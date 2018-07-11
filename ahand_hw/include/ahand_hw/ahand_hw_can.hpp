#ifndef AHAND_HW_CAN_H
#define AHAND_HW_CAN_H

#include <angles/angles.h>

#include "ahand_hw/ahand_hw.h"
#include "ahand_driver/ahandDriver.h"

#include <angles/angles.h>

class AhandHWCAN : public AhandHW {

public:

    AhandHWCAN() : AhandHW() {}

    void stop(){
        ahandDriver_->stop();
        delete ahandDriver_;
        ahandDriver_ = NULL;
    }

    bool init(){
        ahandDriver_ = new AhandDriver();
        return ahandDriver_->isIntialised();
    }

    void read(ros::Time time, ros::Duration period){
        ahandDriver_->getJointInfo(positions_);

        for(std::size_t j=0; j < n_joints_; j++){
            joint_position_prev_[j] = joint_position_[j];
            joint_position_[j] = positions_[j];//+= angles::shortest_angular_distance(joint_position_[j], positions_[j]);
            joint_velocity_[j] = filters::exponentialSmoothing((joint_position_[j] - joint_position_prev_[j])/period.toSec(), joint_velocity_[j], 0.2);
            joint_effort_[j]   = 0.0;
        }
    }

    void write(ros::Time time, ros::Duration period){
        for(int i = 1; i < n_joints_; i++){
            joint_effort_command_[i] = 0;
        }

        ahandDriver_->setTorque(&joint_effort_command_[0]);
    }

private:

    AhandDriver* ahandDriver_;
    double positions_[n_joints_];
    double torques_[n_joints_];

};


#endif
