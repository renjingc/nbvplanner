#include <ros/ros.h>
#include <prune_pointcloud/prune.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl_ros/transforms.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/PoseStamped.h>

#define SQ(x) ((x)*(x))

PointcloudPruning::Prune::Prune(ros::NodeHandle& n)
{
  n_ = n;
  pointcloudSub_ = n_.subscribe("pointcloudIn", 40, &PointcloudPruning::Prune::pointcloud, this);
  poseSub_ = n_.subscribe("pose", 40, &PointcloudPruning::Prune::pose, this);
  pcl_publisher_ = n_.advertise<sensor_msgs::PointCloud2>("pointcloudOut", 1, true);
}

PointcloudPruning::Prune::~Prune()
{
}

void PointcloudPruning::Prune::pointcloud(const sensor_msgs::PointCloud2::ConstPtr& pointcloudIn)
{
  std::vector < tf::Vector3 > agents;
  for (typename std::vector<std::pair<std::string, double> >::iterator it =
      vehicle_tf_frames_.begin(); it != vehicle_tf_frames_.end(); it++) {
    tf::StampedTransform tf_transform;
    ros::Time time_to_lookup = pointcloudIn->header.stamp;
    if (!tf_listener_.canTransform(it->first, pointcloudIn->header.frame_id, time_to_lookup)) {
      time_to_lookup = ros::Time(0);
      ROS_WARN("Using latest TF transform instead of timestamp match.");
    }
    try {
      tf_listener_.lookupTransform(it->first, pointcloudIn->header.frame_id, time_to_lookup,
                                   tf_transform);
    } catch (tf::TransformException& ex) {
      ROS_ERROR_STREAM("Error getting TF transform from sensor data: " << ex.what());
      return;
    }
    agents.push_back(tf_transform.getOrigin());
  }
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::fromROSMsg(*pointcloudIn, *cloud);
  for (pcl::PointCloud<pcl::PointXYZ>::iterator it = cloud->begin(); it != cloud->end(); ++it) {
    for (typename std::vector<tf::Vector3>::iterator itPose = agents.begin();
        itPose != agents.end(); itPose++) {
      if (SQ(it->x - itPose->x()) + SQ(it->y - itPose->x()) + SQ(it->z - itPose->x()) < maxDist2_) {
        cloud->erase(it);
      }
    }
  }
  sensor_msgs::PointCloud2::Ptr pointcloudOut(new sensor_msgs::PointCloud2);
  pcl::toROSMsg(*cloud, *pointcloudOut);
  pcl_publisher_.publish(pointcloudOut);
}

void PointcloudPruning::Prune::pose(const geometry_msgs::PoseStamped& pose)
{
  double now = ros::Time::now().toSec();
  for (typename std::vector<std::pair<std::string, double> >::iterator it =
      vehicle_tf_frames_.begin(); it != vehicle_tf_frames_.end(); it++) {
    if (it->first == pose.header.frame_id) {
      return;
    }
    if (now - it->second > store_duration_) {
      vehicle_tf_frames_.erase(it);
    }
  }
  vehicle_tf_frames_.push_back(
      std::pair<std::string, double>(pose.header.frame_id, ros::Time::now().toSec()));
}

void PointcloudPruning::Prune::loadParams()
{

  n_.param("max_dist", maxDist2_, 0.5);
  n_.param("store_duration", store_duration_, 2.0);
  maxDist2_ = pow(maxDist2_, 2.0);
}
