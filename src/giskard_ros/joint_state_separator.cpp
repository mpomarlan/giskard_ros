/*
* Copyright (C) 2016-2017 Georg Bartels <georg.bartels@cs.uni-bremen.de>
*
*
* This file is part of giskard.
*
* giskard is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2 
* of the License, or (at your option) any later version.  
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License 
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <exception>
#include <ros/ros.h>
#include <boost/lexical_cast.hpp>
#include <sensor_msgs/JointState.h>
#include <std_msgs/Float64.h>
#include <giskard_ros/ros_utils.hpp>

#include <urdf/model.h>
#include <geometry_msgs/Twist.h>

namespace giskard_ros
{
  class JointStateSeparator
  {
    public:
      JointStateSeparator(const ros::NodeHandle& nh) : nh_(nh), robot_model_loaded_(false), output_twist_(false) {}
      ~JointStateSeparator() {}

      void start() 
      {
        std::string robot_description_parameter = (nh_.hasParam("robot_description_parameter_name"))?readParam<std::string>(nh_, "robot_description_parameter_name"):"/robot_description";
        output_twist_ = (nh_.hasParam("output_twist"))?readParam<bool>(nh_, "output_twist"):false;
        linear_scale_ = (nh_.hasParam("linear_scale"))?readParam<double>(nh_, "linear_scale"):1.0;
        angular_scale_ = (nh_.hasParam("angular_scale"))?readParam<double>(nh_, "angular_scale"):1.0;
        std::vector<double> x_def(3, 0.0), y_def(3, 0.0), z_def(3, 0.0);
        x_def[0] = y_def[1] = z_def[2] = 1.0;
        x_axis_ = (nh_.hasParam("x_axis"))?readParam<std::vector<double> >(nh_, "x_axis"):x_def;
        y_axis_ = (nh_.hasParam("y_axis"))?readParam<std::vector<double> >(nh_, "y_axis"):y_def;
        z_axis_ = (nh_.hasParam("z_axis"))?readParam<std::vector<double> >(nh_, "z_axis"):z_def;
        if (output_twist_ && !robot_model_.initParam(robot_description_parameter))
        {
            throw std::runtime_error("Could not read urdf from parameter server at '" + robot_description_parameter + "'.");
        }

        joint_names_ = readParam< std::vector<std::string> >(nh_, "joint_names");

        for (size_t i=0; i<joint_names_.size(); ++i)
        {
            if(output_twist_)
            {
                pubs_.push_back(nh_.advertise<geometry_msgs::Twist>("/" + joint_names_[i].substr(0, joint_names_[i].size() - 6) + "_velocity_controller/command", 1));
            }
            else
            {
                pubs_.push_back(nh_.advertise<std_msgs::Float64>("/" + joint_names_[i].substr(0, joint_names_[i].size() - 6) + "_velocity_controller/command", 1));
            }
        }

        sub_ = nh_.subscribe("joint_states", 1, &JointStateSeparator::callback, this);
      }

    private:
      ros::NodeHandle nh_;
      ros::Subscriber sub_;
      std::vector<ros::Publisher> pubs_;
      std::vector<std::string> joint_names_;
      std::vector<double> x_axis_, y_axis_, z_axis_;

      urdf::Model robot_model_;
      bool robot_model_loaded_;
      bool output_twist_;
      double linear_scale_;
      double angular_scale_;

      void callback(const sensor_msgs::JointState::ConstPtr& msg)
      {
        if (msg->name.size() != pubs_.size())
          throw std::runtime_error("Received message with " + boost::lexical_cast<std::string>(msg->name.size()) + " elements but excepted " + boost::lexical_cast<std::string>(pubs_.size()) + " entries.");

        if (msg->velocity.size() != msg->name.size())
          throw std::runtime_error("Received message with " + boost::lexical_cast<std::string>(msg->name.size()) + " names but " + boost::lexical_cast<std::string>(msg->velocity.size()) + " velocities.");

        std::map<std::string, double> name_cmd_map;
        for (size_t i=0; i<msg->name.size(); ++i)
          name_cmd_map.insert( std::pair<std::string, double>(msg->name[i], msg->velocity[i]) );

        for (size_t i=0; i<joint_names_.size(); ++i)
        {
          if (name_cmd_map.find(joint_names_[i]) == name_cmd_map.end())
            throw std::runtime_error("Could not find velocity command for joint '" + joint_names_[i] + "'.");

          if(output_twist_)
          {
            double vel = name_cmd_map.find(joint_names_[i])->second;
            if (robot_model_.joints_.find(joint_names_[i]) == robot_model_.joints_.end() )
                throw std::runtime_error("Could not find link with name '" + joint_names_[i] + "'.");
            urdf::JointConstSharedPtr joint = robot_model_.joints_.find(joint_names_[i])->second;
            bool prismatic = (joint->type == urdf::Joint::PRISMATIC);
            bool revolute = ((joint->type == urdf::Joint::REVOLUTE) || (joint->type == urdf::Joint::CONTINUOUS));

            geometry_msgs::Twist out_msg;
            out_msg.linear.x = out_msg.linear.y = out_msg.linear.z = 0.0;
            out_msg.angular.x = out_msg.angular.y = out_msg.angular.z = 0.0;
            if(prismatic)
            {
                vel = vel*linear_scale_;
                out_msg.linear.x = joint->axis.x*vel*x_axis_[0] + joint->axis.y*vel*y_axis_[0] + joint->axis.z*vel*z_axis_[0];
                out_msg.linear.y = joint->axis.x*vel*x_axis_[1] + joint->axis.y*vel*y_axis_[1] + joint->axis.z*vel*z_axis_[1];
                out_msg.linear.z = joint->axis.x*vel*x_axis_[2] + joint->axis.y*vel*y_axis_[2] + joint->axis.z*vel*z_axis_[2];
            }
            if(revolute)
            {
                vel = vel*angular_scale_;
                out_msg.angular.x = joint->axis.x*vel*x_axis_[0] + joint->axis.y*vel*y_axis_[0] + joint->axis.z*vel*z_axis_[0];
                out_msg.angular.y = joint->axis.x*vel*x_axis_[1] + joint->axis.y*vel*y_axis_[1] + joint->axis.z*vel*z_axis_[1];
                out_msg.angular.z = joint->axis.x*vel*x_axis_[2] + joint->axis.y*vel*y_axis_[2] + joint->axis.z*vel*z_axis_[2];
            }
            pubs_[i].publish(out_msg);

          }
          else
          {
              std_msgs::Float64 out_msg;
              out_msg.data = name_cmd_map.find(joint_names_[i])->second;
              pubs_[i].publish(out_msg);
          }
        }
      }
  };
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "joint_state_separator");
  ros::NodeHandle nh("~");

  giskard_ros::JointStateSeparator separator(nh);

  try
  {
    separator.start();
    ros::spin();
  }
  catch (const std::exception& e)
  {
    ROS_ERROR("%s", e.what());
  }

  return 0;
}
