//
// This file is for the test of pairwise point cloud registration using MMLLS-ICP and TEASER
// Dependent 3rd Libs: PCL (>1.7), glog, gflags, TEASER (optional)
// Author: Yue Pan @ ETH Zurich
//

#include "dataio.hpp"
#include "cfilter.hpp"
#include "cregistration.hpp"
#include "map_viewer.h"
#include "build_pose_graph.h"
#include "utility.hpp"
#include "binary_feature_extraction.hpp"

#include <glog/logging.h>
#include <gflags/gflags.h>

//#include <cuda_runtime_api.h>
//#include <cuda.h>

//For visualizer's size (fit pc monitor)
//#define screen_width 2560
//#define screen_height 1440
#define screen_width 1920
#define screen_height 1080

using namespace std;
using namespace lo;

//static void CheckCudaErrorAux(const char *, unsigned, const char *, cudaError_t);
//#define CUDA_CHECK(value) CheckCudaErrorAux(__FILE__, __LINE__, #value, value)

//GFLAG Template: DEFINE_TYPE(Flag_variable_name, default_value, "Comments")
DEFINE_string(point_cloud_1_path, "", "Pointcloud 1 file path");
DEFINE_string(point_cloud_2_path, "", "Pointcloud 2 file path");
DEFINE_string(output_point_cloud_path, "", "Registered source pointcloud file path");
DEFINE_string(appro_coordinate_file, "", "File used to store the approximate station coordinate");

DEFINE_double(cloud_1_down_res, 0.08, "voxel size(m) of downsample for target point cloud"); //default: TLS 0.08, ALS 0.15
DEFINE_double(cloud_2_down_res, 0.08, "voxel size(m) of downsample for source point cloud");
DEFINE_double(gf_grid_size, 2.0, "grid size(m) of ground segmentation");
DEFINE_double(gf_in_grid_h_thre, 0.3, "height threshold(m) above the lowest point in a grid for ground segmentation");
DEFINE_double(gf_neigh_grid_h_thre, 2.2, "height threshold(m) among neighbor grids for ground segmentation");
DEFINE_double(gf_max_h, 5.0, "max height(m) allowed for ground point");
DEFINE_int32(gf_ground_down_rate, 15, "downsampling decimation rate for target ground point cloud");
DEFINE_int32(gf_nonground_down_rate, 3, "downsampling decimation rate for nonground point cloud");
DEFINE_double(pca_neighbor_radius, 1.0, "pca neighborhood searching radius(m) for target point cloud");
DEFINE_int32(pca_neighbor_count, 20, "use only the k nearest neighbor in the r-neighborhood to do PCA");
DEFINE_double(linearity_thre, 0.6, "pca linearity threshold");
DEFINE_double(planarity_thre, 0.6, "pca planarity threshold");
DEFINE_double(curvature_thre, 0.1, "pca local stability threshold");
DEFINE_int32(corr_num, 1000, "number of the correspondence for global coarse registration");
DEFINE_double(corr_dis_thre, 2.0, "distance threshold between correspondence points");
DEFINE_int32(reg_max_iter_num, 25, "Max iteration number for icp-based registration");
DEFINE_double(converge_tran, 0.001, "convergence threshold for translation (in m)");
DEFINE_double(converge_rot_d, 0.01, "convergence threshold for rotation (in degree)");
DEFINE_double(heading_change_step_degree, 15, "The step for the rotation of heading");
DEFINE_bool(realtime_viewer_on, false, "Launch the real-time registration(correspondence) viewer or not"); //if 1, may slow down the registration
DEFINE_bool(is_global_reg, false, "Allow the global registration without good enough initial guess or not");
DEFINE_bool(teaser_on, false, "Using TEASER++ to do the global coarse registration or not");

int main(int argc, char **argv)
{
    google::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging("Mylog_testreg");
    LOG(INFO) << "Launch the program!";
    LOG(INFO) << "Logging is written to " << FLAGS_log_dir;

    pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS); //Ban pcl warnings

    CHECK(FLAGS_point_cloud_1_path != "") << "Need to specify the first point cloud.";
    CHECK(FLAGS_point_cloud_2_path != "") << "Need to specify the second point cloud.";
    CHECK(FLAGS_output_point_cloud_path != "") << "Need to specify where to save the registered point cloud.";

    //Import configuration
    std::string filename1 = FLAGS_point_cloud_1_path;      //The first (target) point cloud's file path
    std::string filename2 = FLAGS_point_cloud_2_path;      //The second (source) point cloud's file path
    std::string filenameR = FLAGS_output_point_cloud_path; //Registered source pointcloud file path
    std::string appro_coordinate_file = FLAGS_appro_coordinate_file;

    float vf_downsample_resolution_source = FLAGS_cloud_1_down_res;
    float vf_downsample_resolution_target = FLAGS_cloud_2_down_res;
    float gf_grid_resolution = FLAGS_gf_grid_size;
    float gf_max_grid_height_diff = FLAGS_gf_in_grid_h_thre;
    float gf_neighbor_height_diff = FLAGS_gf_neigh_grid_h_thre;
    float gf_max_height = FLAGS_gf_max_h;
    int ground_down_rate = FLAGS_gf_ground_down_rate;
    int nonground_down_rate = FLAGS_gf_nonground_down_rate;
    float pca_neigh_r = FLAGS_pca_neighbor_radius;
    int pca_neigh_k = FLAGS_pca_neighbor_count;
    float pca_linearity_thre = FLAGS_linearity_thre;
    float pca_planarity_thre = FLAGS_planarity_thre;
    float pca_curvature_thre = FLAGS_curvature_thre;
    int feature_correspondence_num = FLAGS_corr_num;
    float reg_corr_dis_thre = FLAGS_corr_dis_thre;
    float converge_tran = FLAGS_converge_tran;
    float converge_rot_d = FLAGS_converge_rot_d;
    int max_iteration_num = FLAGS_reg_max_iter_num;
    float heading_step_d = FLAGS_heading_change_step_degree;
    bool launch_realtime_viewer = FLAGS_realtime_viewer_on;
    bool is_global_registration = FLAGS_is_global_reg;
    bool teaser_global_regsitration_on = FLAGS_teaser_on;
    float pca_linearity_thre_down = pca_linearity_thre + 0.1;
    float pca_planarity_thre_down = pca_planarity_thre + 0.1;
    float feature_neighbor_radius = 2.0 * pca_neigh_r;

    DataIo<Point_T> dataio;
    MapViewer<Point_T> viewer(0.0, 0, 1, 1);
    CFilter<Point_T> cfilter;
    CRegistration<Point_T> creg;
    BSCEncoder<Point_T> bsc(feature_neighbor_radius, 5);

    boost::shared_ptr<pcl::visualization::PCLVisualizer> feature_viewer;
    boost::shared_ptr<pcl::visualization::PCLVisualizer> reg_viewer;

    if (launch_realtime_viewer)
    {
        feature_viewer = boost::shared_ptr<pcl::visualization::PCLVisualizer>(new pcl::visualization::PCLVisualizer("Feature Viewer"));
        reg_viewer = boost::shared_ptr<pcl::visualization::PCLVisualizer>(new pcl::visualization::PCLVisualizer("Registration Viewer"));
        viewer.set_interactive_events(feature_viewer, screen_width, screen_height);
        viewer.set_interactive_events(reg_viewer, screen_width, screen_height);
    }

    cloudblock_Ptr cblock_1(new cloudblock_t()), cblock_2(new cloudblock_t());
    cblock_1->filename = filename1;
    cblock_2->filename = filename2;

    //Import the data (5 formats are available: *.pcd, *.las, *.ply, *.txt, *.h5)
    dataio.read_pc_cloud_block(cblock_1);
    dataio.read_pc_cloud_block(cblock_2);

    //Extract feature points
    cfilter.extract_semantic_pts(cblock_1, vf_downsample_resolution_target, gf_grid_resolution, gf_max_grid_height_diff,
                                 gf_neighbor_height_diff, gf_max_height, ground_down_rate, nonground_down_rate,
                                 pca_neigh_r, pca_neigh_k, pca_linearity_thre, pca_planarity_thre, pca_curvature_thre,
                                 pca_linearity_thre_down, pca_planarity_thre_down, false);
    cfilter.extract_semantic_pts(cblock_2, vf_downsample_resolution_source, gf_grid_resolution, gf_max_grid_height_diff,
                                 gf_neighbor_height_diff, gf_max_height, ground_down_rate, nonground_down_rate,
                                 pca_neigh_r, pca_neigh_k, pca_linearity_thre, pca_planarity_thre, pca_curvature_thre,
                                 pca_linearity_thre_down, pca_planarity_thre_down, false);

    if (teaser_global_regsitration_on)
    {
        //refine keypoints
        cfilter.non_max_suppress(cblock_1->pc_vertex, pca_neigh_r);
        cfilter.non_max_suppress(cblock_2->pc_vertex, pca_neigh_r);
    }
    
    //Registration
    constraint_t reg_con;

    //Assign target (cblock1) and source (cblock2) point cloud for registration
    if (!is_global_registration)
        creg.determine_source_target_cloud(cblock_1, cblock_2, reg_con);
    else
    {
        creg.assign_source_target_cloud(cblock_1, cblock_2, reg_con);
        dataio.read_station_position(appro_coordinate_file, "test_station", reg_con.block2->local_station, 1);
    }

    if (teaser_global_regsitration_on)
    {
        //Extract bsc feature
        bsc.extract_bsc_features(reg_con.block1->pc_down, reg_con.block1->pc_vertex, 0, reg_con.block1->keypoint_bsc); //6DOF feature(4) or 4DOF feature(2) or base(1)
        bsc.extract_bsc_features(reg_con.block2->pc_down, reg_con.block2->pc_vertex, 4, reg_con.block2->keypoint_bsc); //6DOF feature(4) or 4DOF feature(2) or base(1)
    }

    if (launch_realtime_viewer)
    {
        viewer.set_pause(1);
        viewer.display_feature_pts_compare_realtime(reg_con.block1, reg_con.block2, feature_viewer);
    }

    Eigen::Matrix4d init_mat;
    init_mat.setIdentity();

    if (teaser_global_regsitration_on)
    {
        pcTPtr target_cor(new pcT()), source_cor(new pcT()), pc_s_init(new pcT());
        //creg.find_feature_correspondence_bsc(reg_con.block1->keypoint_bsc, reg_con.block2->keypoint_bsc, 4,
        //                                     reg_con.block1->pc_vertex, reg_con.block2->pc_vertex, target_cor, source_cor, false);

        creg.find_putable_feature_correspondence_bsc(reg_con.block1->keypoint_bsc, reg_con.block2->keypoint_bsc, 4,
                                                     reg_con.block1->pc_vertex, reg_con.block2->pc_vertex, target_cor, source_cor, feature_correspondence_num);
        creg.coarse_reg_teaser(target_cor, source_cor, init_mat, pca_neigh_r);
        if (launch_realtime_viewer)
        {
            pcl::transformPointCloud(*reg_con.block2->pc_raw, *pc_s_init, init_mat);
            viewer.set_pause(1);
            viewer.display_correspondences(feature_viewer, reg_con.block2->pc_vertex, reg_con.block1->pc_vertex,
                                           reg_con.block2->pc_down, reg_con.block1->pc_down, init_mat);
            viewer.set_pause(1);
            viewer.display_2_pc_compare_realtime(reg_con.block2->pc_raw, reg_con.block1->pc_raw, pc_s_init, reg_con.block1->pc_raw, reg_viewer);
        }
    }

    float reg_corr_dis_thre_min_thre = 2.0 * nonground_down_rate * min_(0.05, max_(vf_downsample_resolution_target, vf_downsample_resolution_source));
    if (!is_global_registration)
        creg.mm_lls_icp(reg_con, max_iteration_num, reg_corr_dis_thre, converge_tran, converge_rot_d, reg_corr_dis_thre_min_thre,
                        1.1, "111110", "1101", 1.0, 0.1, 0.1, 0.1, init_mat);

    else
        creg.mm_lls_icp_4dof_global(reg_con, heading_step_d, max_iteration_num, reg_corr_dis_thre, converge_tran, converge_rot_d, reg_corr_dis_thre_min_thre);

    pcTPtr pc_s_tran(new pcT());
    pcl::transformPointCloud(*reg_con.block2->pc_raw, *pc_s_tran, reg_con.Trans1_2);

    if (launch_realtime_viewer)
    {
        viewer.set_pause(1);
        viewer.display_2_pc_compare_realtime(reg_con.block2->pc_raw, reg_con.block1->pc_raw, pc_s_tran, reg_con.block1->pc_raw, reg_viewer);
    }

    dataio.write_cloud_file(filenameR, pc_s_tran); //Write out the transformed Source Point Cloud

    return 1;
}
