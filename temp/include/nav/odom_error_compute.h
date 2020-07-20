
//
// This file covers the error metrics calculation defined in KITTI Odometry Dataset
// Dependent 3rd Libs: None
//

#ifndef _INCLUDE_ODOM_ERROR_COMPUTE_H
#define _INCLUDE_ODOM_ERROR_COMPUTE_H

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <math.h>
#include <eigen3/Eigen/Dense>

struct odom_errors_t
{
    int first_frame;
    double r_err;
    double t_err;
    double len;
    int len_id;
    double speed;
    odom_errors_t(int first_frame, double r_err, double t_err, double len, int len_id, double speed) : first_frame(first_frame), r_err(r_err), t_err(t_err), len(len), len_id(len_id), speed(speed) {}
};

class OdomErrorCompute
{
public:
    int num_lengths = 8;
    double lengths[8] = {100, 200, 300, 400, 500, 600, 700, 800};
    int step_size = 10; // calculate the error every second (10 frames)

    std::vector<double> trajectoryDistances(const std::vector<Eigen::Matrix4d, Eigen::aligned_allocator<Eigen::Matrix4d>> &poses)
    {
        std::vector<double> dist;
        dist.push_back(0);
        for (int i = 1; i < poses.size(); i++)
        {
            Eigen::MatrixXd P1 = poses[i - 1];
            Eigen::MatrixXd P2 = poses[i];
            double dx = P1(0, 3) - P2(0, 3);
            double dy = P1(1, 3) - P2(1, 3);
            double dz = P1(2, 3) - P2(2, 3);
            dist.push_back(dist[i - 1] + sqrt(dx * dx + dy * dy + dz * dz));
        }
        return dist;
    }

    int lastFrameFromSegmentLength(std::vector<double> &dist, int first_frame, double len)
    {
        for (int i = first_frame; i < dist.size(); ++i)
        {
            if (dist[i] > dist[first_frame] + len)
            {
                return i;
            }
        }
        return -1;
    }

    inline double rotationError(Eigen::Matrix4d &pose_error)
    {
        double a = pose_error(0, 0);
        double b = pose_error(1, 1);
        double c = pose_error(2, 2);
        double d = 0.5 * (a + b + c - 1.0);
        double rot_error_deg = std::acos(std::max(std::min(d, 1.0d), -1.0d));
 
        return rot_error_deg;
    }

    inline double translationError(Eigen::Matrix4d &pose_error)
    {
        double dx = pose_error(0, 3);
        double dy = pose_error(1, 3);
        double dz = pose_error(2, 3);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    //double
    std::vector<odom_errors_t> compute_error(const std::vector<Eigen::Matrix4d, Eigen::aligned_allocator<Eigen::Matrix4d>> &poses_gt,
                                             const std::vector<Eigen::Matrix4d, Eigen::aligned_allocator<Eigen::Matrix4d>> &poses_result)
    {
        // error vector
        std::vector<odom_errors_t> err;

        // pre-compute distances (from ground truth as reference)
        std::vector<double> dist = trajectoryDistances(poses_gt);

        // for all start positions do
        for (int first_frame = 0; first_frame < poses_gt.size(); first_frame += step_size)
        {

            int error_count_temp_frame = 0;

            // for all segment lengths do
            for (int i = 0; i < num_lengths; i++)
            {

                // current length
                double len = lengths[i];

                // compute last frame
                int last_frame = lastFrameFromSegmentLength(dist, first_frame, len);

                // continue, if sequence not long enough
                if (last_frame == -1)
                    continue;

                // compute rotational and translational errors
                Eigen::Matrix4d pose_delta_gt = poses_gt[first_frame].inverse() * poses_gt[last_frame];
                Eigen::Matrix4d pose_delta_result = poses_result[first_frame].inverse() * poses_result[last_frame];
                Eigen::Matrix4d pose_error = pose_delta_result.inverse() * pose_delta_gt;
                double r_err = rotationError(pose_error);
                double t_err = translationError(pose_error);

                // compute speed
                double num_frames = (double)(last_frame - first_frame + 1);
                double speed = len / (0.1 * num_frames);

                error_count_temp_frame++;

                // write to file
                err.push_back(odom_errors_t(first_frame, r_err / len, t_err / len, len, i, speed));
                //std::cout << t_err << std::endl;
            }
        }
        // return error vector
        return err;
    }

    //double
    std::vector<odom_errors_t> compute_error(const std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f>> &true_pose_vec,
                                             const std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f>> &result_pose_vec)
    {
        std::vector<odom_errors_t> err;
        std::vector<Eigen::Matrix4d, Eigen::aligned_allocator<Eigen::Matrix4d>> true_pose_d, result_pose_d;
        for (int i = 0; i < true_pose_vec.size(); i++)
        {
            true_pose_d.push_back(true_pose_vec[i].cast<double>());
            result_pose_d.push_back(result_pose_vec[i].cast<double>());
        }
        err = compute_error(true_pose_d, result_pose_d);
        return err;
    }

    void print_error(std::vector<odom_errors_t> &error)
    {
        //std::cout << "number of calculated error: " << error.size() << std::endl;

        double ATE_table[num_lengths];
        double ARE_table[num_lengths];
        std::vector<int> part_error_num(num_lengths, 0);

        double ATE = 0;
        double ARE = 0;

        for (int len_id = 0; len_id < num_lengths; len_id++)
        {
            ATE_table[len_id] = 0;
            ARE_table[len_id] = 0;
        }

        for (int i = 0; i < error.size(); i++)
        {
            int tmpid = error[i].len_id;
            ATE_table[tmpid] += error[i].t_err;
            ARE_table[tmpid] += error[i].r_err;
            part_error_num[tmpid]++;

            ATE += error[i].t_err;
            ARE += error[i].r_err;
        }

        ATE /= error.size();
        ARE /= error.size();

        for (int len_id = 0; len_id < num_lengths; len_id++)
        {
            if (part_error_num[len_id] > 0)
            {
                ATE_table[len_id] = ATE_table[len_id] / part_error_num[len_id];
                ARE_table[len_id] = ARE_table[len_id] / part_error_num[len_id];
            }
        }

        std::cout << "Accuracy evaluation for your odometry" << std::endl;
        std::cout << "Overall ATE (%) : " << ATE * 100.0 << std::endl;
        std::cout << "Overall ARE (deg/m) : " << 180.0 / M_PI * ARE << std::endl;

        std::cout << "\tdist(m)\t ATE (%) \t ARE (deg/m)\n";

        for (int len_id = 0; len_id < num_lengths; len_id++)
        {
            if (part_error_num[len_id] > 0)
            {
                std::cout << "\t" << 100 * (len_id + 1) << "\t" << std::setw(8) << ATE_table[len_id] * 100.0 << "\t" << std::setw(8) << 180.0 / M_PI * ARE_table[len_id] << "\n";
            }
        }
        std::cout << "Check the leaderboard at http://www.cvlibs.net/datasets/kitti/eval_odometry.php" << std::endl;
    }
};

#endif // _INCLUDE_ODOM_ERROR_COMPUTE_H