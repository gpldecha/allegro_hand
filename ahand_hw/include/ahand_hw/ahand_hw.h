#ifndef AHAND_HW_H
#define AHAND_HW_H

// STL

#include <memory>

// boost
#include <boost/scoped_ptr.hpp>

// ROS headers
#include <std_msgs/Duration.h>
#include <urdf/model.h>

// Eigen

#include <Eigen/Dense>

// ROS controls
#include <hardware_interface/robot_hw.h>
#include <transmission_interface/transmission_info.h>
#include <transmission_interface/transmission_parser.h>
#include <joint_limits_interface/joint_limits.h>
#include <joint_limits_interface/joint_limits_interface.h>
#include <joint_limits_interface/joint_limits_rosparam.h>
#include <joint_limits_interface/joint_limits_urdf.h>
#include <control_toolbox/filters.h>

// KDL
#include <kdl/kdl.hpp>
#include <kdl/tree.hpp>
#include <kdl/chain.hpp>
#include <kdl/chaindynparam.hpp> //this to compute the gravity verctor
#include <kdl_parser/kdl_parser.hpp>


class AhandHW : public hardware_interface::RobotHW {

    public:

        AhandHW();

        virtual bool init() = 0;

        virtual void read(ros::Time time, ros::Duration period) = 0;

        virtual void write(ros::Time time, ros::Duration period) = 0;

        virtual bool canSwitch(const std::list<hardware_interface::ControllerInfo> &start_list, const std::list<hardware_interface::ControllerInfo> &stop_list) const;

        virtual void doSwitch(const std::list<hardware_interface::ControllerInfo> &start_list, const std::list<hardware_interface::ControllerInfo> &stop_list);

    public:

        void create(std::string name, std::string urdf_string);

        void reset();

    private:

        bool parseTransmissionsFromURDF(const std::string& urdf_string);

        void registerInterfaces(const urdf::Model *const urdf_model, std::vector<transmission_interface::TransmissionInfo> transmissions);

        void registerJointLimits(const std::string& joint_name,
                                 const urdf::Model *const urdf_model,
                                 double *const lower_limit,
                                 double *const upper_limit);

        bool initKDLdescription(const urdf::Model *const urdf_model);

    public:

        static const std::size_t n_joints_ = 16; // safe magic number, the allegro hand has 16 joints
        static const std::size_t n_fingers_ = 4;

    public:

        std::array<KDL::Chain, n_fingers_> ahand_chains_;
        std::array<KDL::JntArray, n_fingers_> joint_position_kdl_, gravity_effort_;
        KDL::Vector gravity_;

    public:

        // state and commands
        Eigen::Matrix<double, 16, 1> measured_joint_position_, measured_joint_position_prev, estimated_joint_velocity_, measured_joint_effort_;
        Eigen::Matrix<double, 16, 1> joint_effort_command_;

        // joint limits
        std::vector<double> joint_lower_limits_;
        std::vector<double> joint_upper_limits_;

        // Strings
        std::string robot_namespace_;

        // Model
        std::string urdf_string_;
        urdf::Model urdf_model_;

        // KDL
        std::array<std::unique_ptr<KDL::ChainDynParam>, n_fingers_> f_dyn_solvers_;

        // Transmissions in this plugin's scope
        std::vector<transmission_interface::TransmissionInfo> transmissions_;

        hardware_interface::JointStateInterface       state_interface_;

        hardware_interface::EffortJointInterface      effort_interface_;

        std::vector<std::string> joint_names_;

};

#endif
