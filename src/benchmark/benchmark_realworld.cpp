#include "tools.hpp"
#include <ros/ros.h>
#include <Eigen/Eigenvalues>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <geometry_msgs/PoseArray.h>
#include <random>
#include <ctime>
#include <tf/transform_broadcaster.h>
#include "bavoxel.hpp"

#include <pcl/filters/voxel_grid.h>
#include <pcl/io/pcd_io.h>
#include <malloc.h>


#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/centroid.h>
#include <pcl/common/transforms.h>
#include <pcl/common/eigen.h>
#include <pcl/PointIndices.h>



#include <liblas/liblas.hpp>   // Include libLAS for LAS file output
#include <fstream>             // Include for file stream
#include <chrono>	       // Include for LAS cloud timestamping


#include <pcl/common/common.h>



const std::chrono::system_clock::time_point GPS_EPOCH = std::chrono::system_clock::from_time_t(315964800); // January 6, 1980
const int GPS_UTC_OFFSET = 18; // seconds (as of 2024)



using namespace std;

template <typename T>
void pub_pl_func(T &pl, ros::Publisher &pub)
{
  pl.height = 1; pl.width = pl.size();
  sensor_msgs::PointCloud2 output;
  pcl::toROSMsg(pl, output);
  output.header.frame_id = "camera_init";
  output.header.stamp = ros::Time::now();
  pub.publish(output);
}

ros::Publisher pub_path, pub_test, pub_show, pub_cute;

int read_pose(vector<double> &tims, PLM(3) &rots, PLV(3) &poss, string prename)
{
  string readname = prename + "/alidarPose.csv";

  cout << readname << endl;
  ifstream inFile(readname);

  if(!inFile.is_open())
  {
    printf("open fail\n"); return 0;
  }

  int pose_size = 0;
  string lineStr, str;
  Eigen::Matrix4d aff;
  vector<double> nums;

  int ord = 0;
  while(getline(inFile, lineStr))
  {
    ord++;
    stringstream ss(lineStr);
    while(getline(ss, str, ','))
      nums.push_back(stod(str));

    if(ord == 4)
    {
      for(int j=0; j<16; j++)
        aff(j) = nums[j];

      Eigen::Matrix4d affT = aff.transpose();

      rots.push_back(affT.block<3, 3>(0, 0));
      poss.push_back(affT.block<3, 1>(0, 3));
      tims.push_back(affT(3, 3));
      nums.clear();
      ord = 0;
      pose_size++;
    }
    // if (pose_size==400)
    //   break;
  }

  return pose_size;
}

void read_file(vector<IMUST> &x_buf, vector<pcl::PointCloud<PointType>::Ptr> &pl_fulls, string &prename)
{
  // prename = prename + "/datas/benchmark_realworld/";

  PLV(3) poss; PLM(3) rots;
  vector<double> tims;
  int pose_size = read_pose(tims, rots, poss, prename);
  // pose_size=1200;
  
  // for(int m=0; m<pose_size; m++)
  for(int m=0; m<pose_size; m++)
  {
    // string filename = prename + "full" + to_string(m) + ".pcd";
    string filename = prename + "/"+ to_string(m+0) + ".pcd";
    std::cout<<"filename: "<<filename<<std::endl;
    pcl::PointCloud<PointType>::Ptr pl_ptr(new pcl::PointCloud<PointType>());
    pcl::PointCloud<pcl::PointXYZI> pl_tem;
    pcl::io::loadPCDFile(filename, pl_tem);
    for(pcl::PointXYZI &pp: pl_tem.points)
    {
      PointType ap;
      ap.x = pp.x; ap.y = pp.y; ap.z = pp.z;
      ap.intensity = pp.intensity;
      pl_ptr->push_back(ap);
    }

    pl_fulls.push_back(pl_ptr);

    IMUST curr;
    curr.R = rots[m]; curr.p = poss[m]; curr.t = tims[m];
    x_buf.push_back(curr);
  }
  

}

void data_show(vector<IMUST> x_buf, vector<pcl::PointCloud<PointType>::Ptr> &pl_fulls)
{
  IMUST es0 = x_buf[0];
  for(uint i=0; i<x_buf.size(); i++)
  {
    x_buf[i].p = es0.R.transpose() * (x_buf[i].p - es0.p);
    x_buf[i].R = es0.R.transpose() * x_buf[i].R;
  }

  pcl::PointCloud<PointType> pl_send, pl_path;
  int winsize = x_buf.size();
  for(int i=0; i<winsize; i++)
  {
    pcl::PointCloud<PointType> pl_tem = *pl_fulls[i];
    down_sampling_voxel(pl_tem, 0.05);
    pl_transform(pl_tem, x_buf[i]);
    pl_send += pl_tem;
    //pl_send is glonal map, x_buf[i] is transformation matrix of each frame

    if(i == winsize-1)
    {
      pub_pl_func(pl_send, pub_show);
      pl_send.clear();
      sleep(0.5);
    }

    // if((i%200==0 && i!=0) || i == winsize-1)
    // {
    //   pub_pl_func(pl_send, pub_show);
    //   pl_send.clear();
    //   sleep(0.5);
    // }

    PointType ap;
    ap.x = x_buf[i].p.x();
    ap.y = x_buf[i].p.y();
    ap.z = x_buf[i].p.z();
    ap.curvature = i;
    pl_path.push_back(ap);
  }

  pub_pl_func(pl_path, pub_path);
}

void write_pose(vector<IMUST> x_buf, std::string path)
{
  std::ofstream file;
  file.open(path + "/alidarPoseUpdated.csv", std::ofstream::trunc);
  file.close();
  
  file.open(path + "/alidarPoseUpdated.csv", std::ofstream::app);

  for(size_t cout = 0; cout < x_buf.size(); cout++)
  {
    for (int i = 0; i < x_buf[i].R.rows(); ++i) 
    {
            for (int j = 0; j < x_buf[cout].R.cols(); ++j)
            {
                // Write each element to the CSV file
                file << x_buf[cout].R(i, j);
                file << ",";
            }
            file << x_buf[cout].p[i];
            // Start a new line for the next row
            file << std::endl;
    }
    file <<0<< ","<<0<< ","<<0<< ","<<1<< std::endl;

  }
  file.close();
}

void save_las(vector<IMUST> &x_buf, vector<pcl::PointCloud<PointType>::Ptr> &pl_fulls) {

  pcl::PointCloud<pcl::PointXYZINormal>::Ptr total_cloud (new pcl::PointCloud<pcl::PointXYZINormal> ());

  for (int i = 0; i < x_buf.size(); i++) {
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();

    // (row, column)
    transform (0,0) = x_buf[i].R(0, 0);
    transform (0,1) = x_buf[i].R(0, 1);
    transform (0,2) = x_buf[i].R(0, 2);
    transform (0,3) = x_buf[i].R(0, 3);
    transform (1,0) = x_buf[i].R(1, 0);
    transform (1,1) = x_buf[i].R(1, 1);
    transform (1,2) = x_buf[i].R(1, 2);
    transform (1,3) = x_buf[i].R(1, 3);
    transform (2,0) = x_buf[i].R(2, 0);
    transform (2,1) = x_buf[i].R(2, 1);
    transform (2,2) = x_buf[i].R(2, 2);
    transform (2,3) = x_buf[i].R(2, 3);
    transform (3,0) = x_buf[i].R(3, 0);
    transform (3,1) = x_buf[i].R(3, 1);
    transform (3,2) = x_buf[i].R(3, 2);
    transform (3,3) = x_buf[i].R(3, 3);

    // Print the transformation
    printf ("Method #1: using a Matrix4f\n");
    std::cout << transform << std::endl;

    // Executing the transformation
    pcl::PointCloud<pcl::PointXYZINormal>::Ptr transformed_cloud (new pcl::PointCloud<pcl::PointXYZINormal> ());
    // You can either apply transform_1 or transform_2; they are the same
    pcl::transformPointCloud (*(pl_fulls[i]), *transformed_cloud, transform);

    *total_cloud += *transformed_cloud;
    printf("SIZE OF TOTAL CLOUD: %ld\n", (*total_cloud).size());
  }

  // Get the current system time in UTC
  auto now = std::chrono::system_clock::now();

  // Convert to time_t to use with std::localtime
  std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);

  // Convert to tm structure
  std::tm now_tm = *std::localtime(&now_time_t);

  // Use stringstream to format the time into a string
  std::stringstream ss;
  ss << std::put_time(&now_tm, "%Y-%m-%d-%H-%M-%S");

  // Construct the file name with the .las extension
  std::string file_name = "scans_" + ss.str() + ".las";

  // Construct the full directory path
  std::string all_points_dir = "/home/slam/catkin_ws/data/balm/" + file_name;

  // Log the directory path using ROS_INFO
  std::cout << "Generated file path: " << all_points_dir.c_str() << std::endl;

  // Create an output stream for the LAS file
  std::ofstream ofs;
  ofs.open(all_points_dir, std::ios::out | std::ios::binary);

  // Initialize LAS header
  liblas::Header header;
  header.SetDataFormatId(liblas::ePointFormat1); // Set appropriate format (adjust as needed)
  header.SetPointRecordsCount(0);
  header.SetScale(0.001, 0.001, 0.001);  // Set appropriate scale
  header.SetOffset(0.0, 0.0, 0.0);    // Set appropriate offset

  // Get the current LAS GPS time
  // Calculate the duration in seconds since the GPS epoch
  auto duration_since_gps_epoch = now - GPS_EPOCH;
  double gps_time_seconds = std::chrono::duration_cast<std::chrono::seconds>(duration_since_gps_epoch).count() + GPS_UTC_OFFSET;

  Eigen::Vector4f minPt, maxPt;

  pcl::getMinMax3D(*total_cloud, minPt, maxPt);

  header.SetMin(minPt[0] * 1000 - 1, minPt[1] * 1000 - 1, minPt[2] * 1000 - 1);
  header.SetMax(maxPt[0] * 1000 + 1, maxPt[1] * 1000 + 1, maxPt[2] * 1000 + 1);

  header.SetPointRecordsCount((*total_cloud).size());

  // Initialize LAS writer with the output stream and header
  liblas::Writer writer(ofs, header);

  for (const auto& point : *total_cloud) { // Assuming laserCloudFullRes is your point cloud
      liblas::Point las_point(&header);
      //printf("POINT ADDED: %f, %f, %f\n", point.x, point.y, point.z);
      las_point.SetCoordinates(point.x * 1000, point.y * 1000, point.z * 1000);
      las_point.SetIntensity(point.intensity);
      las_point.SetTime(gps_time_seconds);
      writer.WritePoint(las_point);
  }

  // Close the output file stream
  ofs.close();

  // You can add logging or any additional operations after the LAS file has been written
  std::cout << "LAS file has been written with " << header.GetPointRecordsCount() << " points." << std::endl;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "benchmark2");
  ros::NodeHandle n;
  pub_test = n.advertise<sensor_msgs::PointCloud2>("/map_test", 100);
  pub_path = n.advertise<sensor_msgs::PointCloud2>("/map_path", 100);
  pub_show = n.advertise<sensor_msgs::PointCloud2>("/map_show", 100);
  pub_cute = n.advertise<sensor_msgs::PointCloud2>("/map_cute", 100);

  string prename, ofname;
  vector<IMUST> x_buf;
  vector<pcl::PointCloud<PointType>::Ptr> pl_fulls;

  n.param<double>("voxel_size", voxel_size, 1);
  string file_path;
  n.param<string>("file_path", file_path, "");

  read_file(x_buf, pl_fulls, file_path);

  IMUST es0 = x_buf[0];
  for(uint i=0; i<x_buf.size(); i++)
  {
    x_buf[i].p = es0.R.transpose() * (x_buf[i].p - es0.p);
    x_buf[i].R = es0.R.transpose() * x_buf[i].R;
  }

  win_size = x_buf.size();
  printf("The size of poses: %d\n", win_size);

  data_show(x_buf, pl_fulls);
  printf("Check the point cloud with the initial poses.\n");
  printf("If no problem, input '1' to continue or '0' to exit...\n");
  // int a; cin >> a; if(a==0) exit(0);

  pcl::PointCloud<PointType> pl_full, pl_surf, pl_path, pl_send;
  for(int iterCount=0; iterCount<1; iterCount++)
  { 
    unordered_map<VOXEL_LOC, OCTO_TREE_ROOT*> surf_map;

    eigen_value_array[0] = 1.0 / 16;
    eigen_value_array[1] = 1.0 / 16;
    eigen_value_array[2] = 1.0 / 9;

    for(int i=0; i<win_size; i++)
      cut_voxel(surf_map, *pl_fulls[i], x_buf[i], i);

    pcl::PointCloud<PointType> pl_send;
    pub_pl_func(pl_send, pub_show);

    pcl::PointCloud<PointType> pl_cent; pl_send.clear();
    VOX_HESS voxhess;
    for(auto iter=surf_map.begin(); iter!=surf_map.end() && n.ok(); iter++)
    {
      iter->second->recut(win_size);
      iter->second->tras_opt(voxhess, win_size);
      iter->second->tras_display(pl_send, win_size);
    }

    pub_pl_func(pl_send, pub_cute);
    printf("\nThe planes (point association) cut by adaptive voxelization.\n");
    printf("If the planes are too few, the optimization will be degenerated and fail.\n");
    printf("If no problem, input '1' to continue or '0' to exit...\n");
    // int a; cin >> a; if(a==0) exit(0);
    pl_send.clear(); pub_pl_func(pl_send, pub_cute);

    if(voxhess.plvec_voxels.size() < 3 * x_buf.size())
    {
      printf("Initial error too large.\n");
      printf("Please loose plane determination criteria for more planes.\n");
      printf("The optimization is terminated.\n");
      exit(0);
    }
    
    BALM2 opt_lsv;
    opt_lsv.damping_iter(x_buf, voxhess);

    for(auto iter=surf_map.begin(); iter!=surf_map.end();)
    {
      delete iter->second;
      surf_map.erase(iter++);
    }
    surf_map.clear();

    malloc_trim(0);
  }

  printf("\nRefined point cloud is publishing...\n");
  malloc_trim(0);
  data_show(x_buf, pl_fulls);
  printf("\nRefined point cloud is published.\n");

  printf("\nSaving .las file...\n");
  save_las(x_buf, pl_fulls);
  printf("\nSaved .las file.\n");

  write_pose(x_buf, file_path);
  ros::spin();
  return 0;

}


