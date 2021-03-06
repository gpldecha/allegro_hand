#include "ahand_driver/ahandDriver.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>  //_getch
#include <string.h>
#include <pthread.h>
#include <iostream>

#include <ros/package.h>
#include <fstream>
#include <string>

#define _T(X)   X
#define _tcsicmp(x, y)   strcmp(x, y)

AhandDriver::AhandDriver(const std::string& encoder_conf):
        torque_command_buffer(2), sensor_buffer(2)
{
    memset(&vars, 0, sizeof(vars));

    tau_des.setZero();
    cur_des.setZero();
    q.setZero();
    q_des.setZero();

    if(HAND_VERSION == 1){
        tau_cov_const = tau_cov_const_v2;
    }else{
        tau_cov_const = tau_cov_const_v3;
    }
    std::cout<< "LOADING encoder offsets" << std::endl;
    std::string path = ros::package::getPath("ahand_driver") + "/config/" + encoder_conf;
    std::ifstream infile(path);
    int a, i;
    i = 0;
    while (infile >> a){
        enc_offset[i] = a;
        std::cout<< enc_offset[i] << std::endl;
        i++;
    }


    if(!openCAN())
        std::cerr<< "could not open CAN" << std::endl;
    isInitialised = true;
}

bool AhandDriver::isIntialised(){
    return isInitialised;
}

void AhandDriver::stop(){
    closeCAN();
}

bool AhandDriver::getJointInfo(Vector16d& joint_positions){
    return sensor_buffer.pop(joint_positions);
}

void AhandDriver::setTorque(const Vector16d& torques){
    if(torque_command_buffer.write_available() != 0){
        torque_command_buffer.pop();
    }
    torque_command_buffer.push(torques);
}

bool AhandDriver::openCAN(){
#if defined(PEAKCAN)
  CAN_Ch = GetCANChannelIndex(_T("USBBUS1"));
#elif defined(IXXATCAN)
  CAN_Ch = 1;
#elif defined(SOFTINGCAN)
  CAN_Ch = 1;
#else
  CAN_Ch = 1;
#endif
  CAN_Ch = getCANChannelIndex(_T("USBBUS1"));
  printf(">CAN(%d): open\n", CAN_Ch);
  int ret = CANAPI::command_can_open(CAN_Ch);
  if(ret < 0){
    printf("ERROR command_canopen !!! \n");
    return false;
  }

  ioThreadRun = true;
  updated_thread_ = std::thread(&AhandDriver::updateCAN, this);
  printf(">CAN: starts listening CAN frames\n");

  printf(">CAN: query system id\n");
  ret = CANAPI::command_can_query_id(CAN_Ch);
  if(ret < 0)
    {
      printf("ERROR command_can_query_id !!! \n");
      CANAPI::command_can_close(CAN_Ch);
      return false;
    }

  printf(">CAN: AHRS set\n");
  ret = CANAPI::command_can_AHRS_set(CAN_Ch, AHRS_RATE_100Hz, AHRS_MASK_POSE | AHRS_MASK_ACC);
  if(ret < 0)
    {
      printf("ERROR command_can_AHRS_set !!! \n");
      CANAPI::command_can_close(CAN_Ch);
      return false;
    }

  printf(">CAN: system init\n");
  ret = CANAPI::command_can_sys_init(CAN_Ch, 3/*msec*/);
  if(ret < 0)
    {
      printf("ERROR command_can_sys_init !!! \n");
      CANAPI::command_can_close(CAN_Ch);
      return false;
    }

  printf(">CAN: start periodic communication\n");
  ret = CANAPI::command_can_start(CAN_Ch);

  if(ret < 0)
    {
      printf("ERROR command_can_start !!! \n");
      CANAPI::command_can_stop(CAN_Ch);
      CANAPI::command_can_close(CAN_Ch);
      return false;
    }

  return true;
}

void AhandDriver::closeCAN(){
    printf(">CAN: stop periodic communication\n");
    int ret = CANAPI::command_can_stop(CAN_Ch);
    if(ret < 0)
      {
        printf("ERROR command_can_stop !!! \n");
      }

    if (ioThreadRun)
      {
        printf(">CAN: stoped listening CAN frames\n");
        ioThreadRun = false;
        updated_thread_.join();
      }

    printf(">CAN(%d): close\n", CAN_Ch);
    ret = CANAPI::command_can_close(CAN_Ch);
    if(ret < 0) printf("ERROR command_can_close !!! \n");
}

void AhandDriver::updateCAN(){
    char id_des;
    char id_cmd;
    char id_src;
    int len;
    unsigned char data[8];
    unsigned char data_return = 0;
    int i;

    while (ioThreadRun){
        /* wait for the event */
        while (0 == CANAPI::get_message(CAN_Ch, &id_cmd, &id_src, &id_des, &len, data, FALSE)){

            if (id_src >= ID_DEVICE_SUB_01 && id_src <= ID_DEVICE_SUB_04){
               vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 0] = (int)(data[0] | (data[1] << 8));
               vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 1] = (int)(data[2] | (data[3] << 8));
               vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 2] = (int)(data[4] | (data[5] << 8));
               vars.enc_actual[(id_src-ID_DEVICE_SUB_01)*4 + 3] = (int)(data[6] | (data[7] << 8));
               data_return |= (0x01 << (id_src-ID_DEVICE_SUB_01));
            }
            if (data_return == (0x01 | 0x02 | 0x04 | 0x08)){
                // READ JOINT POSITIONS convert encoder count to joint angle
                for (i=0; i<MAX_DOF; i++){
                    q[i] = (double)(vars.enc_actual[i]*enc_dir[i]-32768-enc_offset[i])*(333.3/65536.0)*(3.141592/180.0);
                }
                if (sensor_buffer.write_available() == 0) {
                    sensor_buffer.pop();
                }
                sensor_buffer.push(q);

                torque_command_buffer.pop(tau_des);
                //std::cout<< "tau_des " << tau_des.transpose() << std::endl;

                // convert desired torque to desired current and PWM count
                for (i=0; i<MAX_DOF; i++){
                   cur_des[i] = tau_des[i]*motor_dir[i];
                   if (cur_des[i] > 1.0) cur_des[i] = 1.0;
                   else if (cur_des[i] < -1.0) cur_des[i] = -1.0;
                }

                // send PWM count
                for (int i=0; i<4;i++){
                    // the index order for motors is different from that of encoders
                    vars.pwm_demand[i*4+3] = (short)(cur_des[i*4+0]*tau_cov_const);
                    vars.pwm_demand[i*4+2] = (short)(cur_des[i*4+1]*tau_cov_const);
                    vars.pwm_demand[i*4+1] = (short)(cur_des[i*4+2]*tau_cov_const);
                    vars.pwm_demand[i*4+0] = (short)(cur_des[i*4+3]*tau_cov_const);
                    CANAPI::write_current(CAN_Ch, i, &vars.pwm_demand[4*i]);
                    usleep(5);
                }
                data_return = 0;
            }
       }
    }
}

int AhandDriver::getCANChannelIndex(const char* cname)
{
  if (!cname) return 0;

  if (!_tcsicmp(cname, _T("0")) || !_tcsicmp(cname, _T("PCAN_NONEBUS")) || !_tcsicmp(cname, _T("NONEBUS")))
    return 0;
  else if (!_tcsicmp(cname, _T("1")) || !_tcsicmp(cname, _T("PCAN_ISABUS1")) || !_tcsicmp(cname, _T("ISABUS1")))
    return 1;
  else if (!_tcsicmp(cname, _T("2")) || !_tcsicmp(cname, _T("PCAN_ISABUS2")) || !_tcsicmp(cname, _T("ISABUS2")))
    return 2;
  else if (!_tcsicmp(cname, _T("3")) || !_tcsicmp(cname, _T("PCAN_ISABUS3")) || !_tcsicmp(cname, _T("ISABUS3")))
    return 3;
  else if (!_tcsicmp(cname, _T("4")) || !_tcsicmp(cname, _T("PCAN_ISABUS4")) || !_tcsicmp(cname, _T("ISABUS4")))
    return 4;
  else if (!_tcsicmp(cname, _T("5")) || !_tcsicmp(cname, _T("PCAN_ISABUS5")) || !_tcsicmp(cname, _T("ISABUS5")))
    return 5;
  else if (!_tcsicmp(cname, _T("7")) || !_tcsicmp(cname, _T("PCAN_ISABUS6")) || !_tcsicmp(cname, _T("ISABUS6")))
    return 6;
  else if (!_tcsicmp(cname, _T("8")) || !_tcsicmp(cname, _T("PCAN_ISABUS7")) || !_tcsicmp(cname, _T("ISABUS7")))
    return 7;
  else if (!_tcsicmp(cname, _T("8")) || !_tcsicmp(cname, _T("PCAN_ISABUS8")) || !_tcsicmp(cname, _T("ISABUS8")))
    return 8;
  else if (!_tcsicmp(cname, _T("9")) || !_tcsicmp(cname, _T("PCAN_DNGBUS1")) || !_tcsicmp(cname, _T("DNGBUS1")))
    return 9;
  else if (!_tcsicmp(cname, _T("10")) || !_tcsicmp(cname, _T("PCAN_PCIBUS1")) || !_tcsicmp(cname, _T("PCIBUS1")))
    return 10;
  else if (!_tcsicmp(cname, _T("11")) || !_tcsicmp(cname, _T("PCAN_PCIBUS2")) || !_tcsicmp(cname, _T("PCIBUS2")))
    return 11;
  else if (!_tcsicmp(cname, _T("12")) || !_tcsicmp(cname, _T("PCAN_PCIBUS3")) || !_tcsicmp(cname, _T("PCIBUS3")))
    return 12;
  else if (!_tcsicmp(cname, _T("13")) || !_tcsicmp(cname, _T("PCAN_PCIBUS4")) || !_tcsicmp(cname, _T("PCIBUS4")))
    return 13;
  else if (!_tcsicmp(cname, _T("14")) || !_tcsicmp(cname, _T("PCAN_PCIBUS5")) || !_tcsicmp(cname, _T("PCIBUS5")))
    return 14;
  else if (!_tcsicmp(cname, _T("15")) || !_tcsicmp(cname, _T("PCAN_PCIBUS6")) || !_tcsicmp(cname, _T("PCIBUS6")))
    return 15;
  else if (!_tcsicmp(cname, _T("16")) || !_tcsicmp(cname, _T("PCAN_PCIBUS7")) || !_tcsicmp(cname, _T("PCIBUS7")))
    return 16;
  else if (!_tcsicmp(cname, _T("17")) || !_tcsicmp(cname, _T("PCAN_PCIBUS8")) || !_tcsicmp(cname, _T("PCIBUS8")))
    return 17;
  else if (!_tcsicmp(cname, _T("18")) || !_tcsicmp(cname, _T("PCAN_USBBUS1")) || !_tcsicmp(cname, _T("USBBUS1")))
    return 18;
  else if (!_tcsicmp(cname, _T("19")) || !_tcsicmp(cname, _T("PCAN_USBBUS2")) || !_tcsicmp(cname, _T("USBBUS2")))
    return 19;
  else if (!_tcsicmp(cname, _T("20")) || !_tcsicmp(cname, _T("PCAN_USBBUS3")) || !_tcsicmp(cname, _T("USBBUS3")))
    return 20;
  else if (!_tcsicmp(cname, _T("21")) || !_tcsicmp(cname, _T("PCAN_USBBUS4")) || !_tcsicmp(cname, _T("USBBUS4")))
    return 21;
  else if (!_tcsicmp(cname, _T("22")) || !_tcsicmp(cname, _T("PCAN_USBBUS5")) || !_tcsicmp(cname, _T("USBBUS5")))
    return 22;
  else if (!_tcsicmp(cname, _T("23")) || !_tcsicmp(cname, _T("PCAN_USBBUS6")) || !_tcsicmp(cname, _T("USBBUS6")))
    return 23;
  else if (!_tcsicmp(cname, _T("24")) || !_tcsicmp(cname, _T("PCAN_USBBUS7")) || !_tcsicmp(cname, _T("USBBUS7")))
    return 24;
  else if (!_tcsicmp(cname, _T("25")) || !_tcsicmp(cname, _T("PCAN_USBBUS8")) || !_tcsicmp(cname, _T("USBBUS8")))
    return 25;
  else if (!_tcsicmp(cname, _T("26")) || !_tcsicmp(cname, _T("PCAN_PCCBUS1")) || !_tcsicmp(cname, _T("PCCBUS1")))
    return 26;
  else if (!_tcsicmp(cname, _T("27")) || !_tcsicmp(cname, _T("PCAN_PCCBUS2")) || !_tcsicmp(cname, _T("PCCBUS2")))
    return 271;
  else
    return 0;
}

