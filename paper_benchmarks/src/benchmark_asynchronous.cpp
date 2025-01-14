#include "paper_benchmarks/benchmark_asynchronous.hpp"
#include <thread>
#include <iostream>
#include <string>
#include "std_msgs/msg/string.hpp"
#include "paper_benchmarks/cube_selector.hpp"

using namespace std::chrono_literals;

struct arm_state
{
    const moveit::core::JointModelGroup *arm_joint_model_group;
    const std::vector<std::string> &arm_joint_names;
    std::vector<double> arm_joint_values;
    geometry_msgs::msg::Pose pose;
    CollisionPlanningObject object;

    arm_state(const moveit::core::JointModelGroup *jmg) : arm_joint_model_group(jmg), arm_joint_names(jmg->getVariableNames()) {}
};

bool advancedExecuteTrajectory(arm_state &arm_1_state, moveit::planning_interface::MoveGroupInterface &panda_1_arm,  
  moveit::core::RobotModelConstPtr kinematic_model, moveit::core::RobotStatePtr kinematic_state, 
  moveit_msgs::msg::CollisionObject &object, tray_helper *tray, int s);


int number_of_test_cases = 5;

static struct runner{
  int counter = 0;
  std::mutex mtx;

  void increment(){
    std::lock_guard<std::mutex> lock(mtx);
    counter++;
  }

  int check(){
    std::lock_guard<std::mutex> lock(mtx);
    return counter;
  }
} runner2;

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);

  node = std::make_shared<rclcpp::Node>("benchmark_asynchronous");

  node->declare_parameter("launchType", "randomDistance");
  node->declare_parameter("cubesToPick", 5);

  std::string distanceType = node->get_parameter("launchType").as_string();
  
  number_of_test_cases = node->get_parameter("cubesToPick").as_int();

  RCLCPP_INFO(LOGGER, "launch: %s", distanceType.c_str());

  pnp_1 = std::make_shared<primitive_pick_and_place>(node, "panda_1");
  pnp_2 = std::make_shared<primitive_pick_and_place>(node, "panda_2");
  
  publisher_ = node->create_publisher<std_msgs::msg::String>("spawnNewCube", 10);

  new std::thread(update_planning_scene);

  new std::thread(main_thread);

  rclcpp::spin(node);
  rclcpp::shutdown();
}

void update_planning_scene()
{

  while (true)
  {

    objMap = pnp_1->getCollisionObjects();
    colors = pnp_1->getCollisionObjectColors();

    for (auto &pair : objMap)
    {
      auto it = std::find(all_objects.begin(), all_objects.end(), pair.second.id);

      // not found in the object list
      if (it == all_objects.end())
      {
        all_objects.push_back(pair.second.id);

        CollisionPlanningObject new_object(pair.second, 0, 0);
        objs.push(new_object);

        RCLCPP_INFO(LOGGER, "New object detected. id: %s", pair.second.id.c_str());
      }
    }

    if(objs.size() < 4)
    {
      auto message = std_msgs::msg::String();
      for(int i = 0; i < 4; i++){
        publisher_->publish(message);
      }
    }

    std::this_thread::sleep_for(1.0s);
    update_scene_called_once = true;
  }
}

void main_thread()
{
  //rclcpp::Rate r();

  pnp_1->home();
  pnp_2->home();

  pnp_1->open_gripper();
  pnp_2->open_gripper();

  moveit::planning_interface::MoveGroupInterface panda_1_arm(node, "panda_1");
  panda_1_arm.setMaxVelocityScalingFactor(0.50);
  panda_1_arm.setMaxAccelerationScalingFactor(0.50);
  panda_1_arm.setNumPlanningAttempts(5);
  panda_1_arm.setPlanningTime(1);

  moveit::planning_interface::MoveGroupInterface panda_2_arm(node, "panda_2");
  panda_2_arm.setMaxVelocityScalingFactor(0.50);
  panda_2_arm.setMaxAccelerationScalingFactor(0.5);
  panda_2_arm.setNumPlanningAttempts(5);
  panda_2_arm.setPlanningTime(1);

  moveit::core::RobotModelConstPtr kinematic_model = panda_1_arm.getRobotModel();
  moveit::core::RobotStatePtr kinematic_state = panda_1_arm.getCurrentState();

  moveit::core::RobotModelConstPtr kinematic_model_2 = panda_1_arm.getRobotModel();
  moveit::core::RobotStatePtr kinematic_state_2 = panda_1_arm.getCurrentState();

  arm_state arm_1_state(kinematic_model->getJointModelGroup("panda_1"));
  arm_state arm_2_state(kinematic_model_2->getJointModelGroup("panda_2"));
  
  
  while (!update_scene_called_once)
  {
    std::this_thread::sleep_for(1.0s);
  }

  RCLCPP_INFO(LOGGER, "Size: %li", objs.size());

  int i = 0;

  RCLCPP_INFO(LOGGER, "[checkpoint] Starting execution");

  bool planned_for_panda_1 = false;
  bool planned_for_panda_2 = false;

  while (!objs.empty())
  {
    CollisionPlanningObject current_object;
    std::string curren_planning_robot = "robot_1";  

    // start planning if atleast one of the arms are available
    if (!panda_1_busy || !panda_2_busy)
    {
      // change end effector position based on the robot available
      if (!panda_1_busy)
      {
        e.x = 0;
        e.y = -0.5;
        e.z = 1;
        curren_planning_robot = "robot_1";
      }
      else //if (!planned_for_panda_2)
      {
        e.x = 0;
        e.y = 0.5;
        e.z = 1;
        curren_planning_robot = "robot_2";
      }

      objs.updatePoint(e);
      //current_object = objs.pop(curren_planning_robot, "random");
      current_object = objs.pop(curren_planning_robot, "");


      auto object_id = current_object.collisionObject.id;
      RCLCPP_INFO(LOGGER, "Object: %s", object_id.c_str());

      // Check if the object is a box
      if (object_id.rfind("box", 0) != 0)
      {
        continue;
      }

      bool panda_1_success = true;
      
      // plan for if the arm one is not busy
      if (!panda_1_busy)
      {
        tray_helper *active_tray;
        if (colors[object_id].color.r == 1 && colors[object_id].color.g == 0 && colors[object_id].color.b == 0)
          active_tray = &red_tray_1;
        else if (colors[object_id].color.r == 0 && colors[object_id].color.g == 0 && colors[object_id].color.b == 1)
          active_tray = &blue_tray_1;
        else
          continue;

        panda_1_busy = true;

        new std::thread([&]()
                        {
          
          auto current_object_1 = std::move(current_object);
          //bool panda_1_success = executeTrajectory(pnp_1, current_object_1.collisionObject,active_tray);
          bool panda_1_success = advancedExecuteTrajectory(arm_1_state, panda_1_arm, kinematic_model, 
          kinematic_state, current_object_1.collisionObject,active_tray, 1);
          
          if(!panda_1_success)
          {
            objs.push(current_object_1);
          }else{
            runner2.increment();
            RCLCPP_INFO(LOGGER, "[checkpoint] Robot 1 successful placing. Request to spawn a new cube ");
            
            if(runner2.check() >= number_of_test_cases)
              RCLCPP_INFO(LOGGER, "[terminate]");
          
            // auto message = std_msgs::msg::String();

            // if(runner2.check() % 4 == 0){
            //   for(int i = 0; i < 4; i++){
            //     publisher_->publish(message);
            //   }
            // }
            
          }
          panda_1_busy = false; });

        //std::this_thread::sleep_for(3.s);
        //r.sleep();
      }
      else if (!panda_2_busy)
      {
        planned_for_panda_2 = false;
        tray_helper *active_tray;
        if (colors[object_id].color.r == 1 && colors[object_id].color.g == 0 && colors[object_id].color.b == 0)
          active_tray = &red_tray_2;
        else if (colors[object_id].color.r == 0 && colors[object_id].color.g == 0 && colors[object_id].color.b == 1)
          active_tray = &blue_tray_2;
        else
          continue;

        panda_2_busy = true;

        new std::thread([&]()
                        {
          
          auto current_object_2 = std::move(current_object);
          //bool panda_1_success = executeTrajectory(pnp_1, current_object_1.collisionObject,active_tray);
          bool panda_2_success = advancedExecuteTrajectory(arm_2_state, panda_2_arm, kinematic_model_2, 
          kinematic_state_2, current_object_2.collisionObject, active_tray, 2);
          
          if(!panda_2_success)
          {
            objs.push(current_object_2);
          }else{
            runner2.increment();
            RCLCPP_INFO(LOGGER, "[checkpoint] Robot 2 successful placing. Request to spawn a new cube ");
            
            if(runner2.check() >= number_of_test_cases)
              RCLCPP_INFO(LOGGER, "[terminate]");
          
            // auto message = std_msgs::msg::String();
            //publisher_->publish(message);
          }
          panda_2_busy = false; });
        
        //std::this_thread::sleep_for(3.s);
      }
    }
    std::this_thread::sleep_for(3.s);

    //r.sleep();
  }

  RCLCPP_INFO(LOGGER, "Execution completed");
}

bool advancedExecuteTrajectory(arm_state &arm_1_state, moveit::planning_interface::MoveGroupInterface &panda_1_arm,  
  moveit::core::RobotModelConstPtr kinematic_model, moveit::core::RobotStatePtr kinematic_state, 
  moveit_msgs::msg::CollisionObject &object, tray_helper *tray, int s)
{

  RCLCPP_INFO(LOGGER, "Start execution of Object: %s", object.id.c_str());
  geometry_msgs::msg::Pose pose;

  // Pre Grasp
  arm_1_state.pose.position.x = object.pose.position.x;
  arm_1_state.pose.position.y = object.pose.position.y;
  arm_1_state.pose.position.z = object.pose.position.z + 0.25;

  arm_1_state.pose.orientation.x = object.pose.orientation.w;
  arm_1_state.pose.orientation.y = object.pose.orientation.z;
  arm_1_state.pose.orientation.z = 0;
  arm_1_state.pose.orientation.w = 0;

  bool executionSuccessful = false;

  RCLCPP_INFO(LOGGER, "Set from ik done ");
  
  while (!executionSuccessful)
  {

    bool a_bot_found_ik = kinematic_state->setFromIK(arm_1_state.arm_joint_model_group, arm_1_state.pose, 0.1);

    RCLCPP_INFO(LOGGER, "Starting pregrasp execution ");

    if (a_bot_found_ik)
    {
      kinematic_state->copyJointGroupPositions(arm_1_state.arm_joint_model_group, arm_1_state.arm_joint_values);
      panda_1_arm.setJointValueTarget(arm_1_state.arm_joint_names, arm_1_state.arm_joint_values);
      RCLCPP_INFO(LOGGER, "IK found for arm 1");
    }else{
      continue;
    }
    
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    if (a_bot_found_ik)
    {
      bool success = (panda_1_arm.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);
      if(success){
        RCLCPP_INFO(LOGGER, "pregrasp planning successful");
      }
      if (!success || my_plan.trajectory_.joint_trajectory.points.size() == 0)
      {
        RCLCPP_INFO(LOGGER, "Plan did not succeed");
        return false;
      }
      executionSuccessful = panda_1_arm.execute(my_plan) == moveit::core::MoveItErrorCode::SUCCESS;
    }
  }


  // Pre Grasp
  arm_1_state.pose.position.z = object.pose.position.z + 0.1;

  executionSuccessful = false;
  
  while (!executionSuccessful)
  {

    bool a_bot_found_ik = kinematic_state->setFromIK(arm_1_state.arm_joint_model_group, arm_1_state.pose, 0.1);

    RCLCPP_INFO(LOGGER, "Starting grasp execution ");

    if (a_bot_found_ik)
    {
      kinematic_state->copyJointGroupPositions(arm_1_state.arm_joint_model_group, arm_1_state.arm_joint_values);
      panda_1_arm.setJointValueTarget(arm_1_state.arm_joint_names, arm_1_state.arm_joint_values);
      RCLCPP_INFO(LOGGER, "IK found for arm 1");
    }else{
      continue;
    }
    
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    if (a_bot_found_ik)
    {
      bool success = (panda_1_arm.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);
      if(success){
        RCLCPP_INFO(LOGGER, "grasp planning successful");
      }
      if (!success || my_plan.trajectory_.joint_trajectory.points.size() == 0)
      {
        RCLCPP_INFO(LOGGER, "Plan did not succeed");
        return false;
      }
      executionSuccessful = panda_1_arm.execute(my_plan) == moveit::core::MoveItErrorCode::SUCCESS;
    }
  }


  // pre move
  if(s == 1){
    pnp_1->grasp_object(object);
  }else if(s == 2){
    pnp_2->grasp_object(object);
  }

  arm_1_state.pose.position.z = object.pose.position.z + 0.25;

  executionSuccessful = false;
  
  while (!executionSuccessful)
  {

    bool a_bot_found_ik = kinematic_state->setFromIK(arm_1_state.arm_joint_model_group, arm_1_state.pose, 0.1);

    RCLCPP_INFO(LOGGER, "Starting premove execution ");

    if (a_bot_found_ik)
    {
      kinematic_state->copyJointGroupPositions(arm_1_state.arm_joint_model_group, arm_1_state.arm_joint_values);
      panda_1_arm.setJointValueTarget(arm_1_state.arm_joint_names, arm_1_state.arm_joint_values);
      RCLCPP_INFO(LOGGER, "IK found for arm 1");
    }else{
      continue;
    }
    
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    if (a_bot_found_ik)
    {
      bool success = (panda_1_arm.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);
      if(success){
        RCLCPP_INFO(LOGGER, "premove planning successful");
      }
      if (!success || my_plan.trajectory_.joint_trajectory.points.size() == 0)
      {
        RCLCPP_INFO(LOGGER, "Plan did not succeed");
        continue;
      }
      executionSuccessful = panda_1_arm.execute(my_plan) == moveit::core::MoveItErrorCode::SUCCESS;
    }
  }

  // Move
  arm_1_state.pose.position.x = tray->get_x();
  arm_1_state.pose.position.y = tray->get_y();
  arm_1_state.pose.position.z = 1.28 + tray->z * 0.05;

  arm_1_state.pose.orientation.x = 1;
  arm_1_state.pose.orientation.y = 0;

  executionSuccessful = false;
  
  while (!executionSuccessful)
  {

    bool a_bot_found_ik = kinematic_state->setFromIK(arm_1_state.arm_joint_model_group, arm_1_state.pose, 0.1);

    RCLCPP_INFO(LOGGER, "Starting premove execution ");

    if (a_bot_found_ik)
    {
      kinematic_state->copyJointGroupPositions(arm_1_state.arm_joint_model_group, arm_1_state.arm_joint_values);
      panda_1_arm.setJointValueTarget(arm_1_state.arm_joint_names, arm_1_state.arm_joint_values);
      RCLCPP_INFO(LOGGER, "IK found for arm 1");
    }else{
      continue;
    }
    
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    if (a_bot_found_ik)
    {
      bool success = (panda_1_arm.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);
      if(success){
        RCLCPP_INFO(LOGGER, "premove planning successful");
      }
      if (!success || my_plan.trajectory_.joint_trajectory.points.size() == 0)
      {
        RCLCPP_INFO(LOGGER, "Plan did not succeed");
        continue;
      }
      executionSuccessful = panda_1_arm.execute(my_plan) == moveit::core::MoveItErrorCode::SUCCESS;
    }
  }
  // Put down
  arm_1_state.pose.position.z = 1.141 + tray->z * 0.05;

  executionSuccessful = false;
  
  while (!executionSuccessful)
  {

    bool a_bot_found_ik = kinematic_state->setFromIK(arm_1_state.arm_joint_model_group, arm_1_state.pose, 0.1);

    RCLCPP_INFO(LOGGER, "Starting premove execution ");

    if (a_bot_found_ik)
    {
      kinematic_state->copyJointGroupPositions(arm_1_state.arm_joint_model_group, arm_1_state.arm_joint_values);
      panda_1_arm.setJointValueTarget(arm_1_state.arm_joint_names, arm_1_state.arm_joint_values);
      RCLCPP_INFO(LOGGER, "IK found for arm 1");
    }else{
      continue;
    }
    
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    if (a_bot_found_ik)
    {
      bool success = (panda_1_arm.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);
      if(success){
        RCLCPP_INFO(LOGGER, "premove planning successful");
      }
      if (!success || my_plan.trajectory_.joint_trajectory.points.size() == 0)
      {
        RCLCPP_INFO(LOGGER, "Plan did not succeed");
        continue;
      }
      executionSuccessful = panda_1_arm.execute(my_plan) == moveit::core::MoveItErrorCode::SUCCESS;
    }
  }

  if(s == 1){
    pnp_1->release_object(object);
  }else if(s == 2){
    pnp_2->release_object(object);
  }
  // Post Move
  arm_1_state.pose.position.z = 1.28 + tray->z * 0.05;

  executionSuccessful = false;
  
  while (!executionSuccessful)
  {

    bool a_bot_found_ik = kinematic_state->setFromIK(arm_1_state.arm_joint_model_group, arm_1_state.pose, 0.1);

    RCLCPP_INFO(LOGGER, "Starting premove execution ");

    if (a_bot_found_ik)
    {
      kinematic_state->copyJointGroupPositions(arm_1_state.arm_joint_model_group, arm_1_state.arm_joint_values);
      panda_1_arm.setJointValueTarget(arm_1_state.arm_joint_names, arm_1_state.arm_joint_values);
      RCLCPP_INFO(LOGGER, "IK found for arm 1");
    }else{
      continue;
    }
    
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    if (a_bot_found_ik)
    {
      bool success = (panda_1_arm.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);
      if(success){
        RCLCPP_INFO(LOGGER, "premove planning successful");
      }
      if (!success || my_plan.trajectory_.joint_trajectory.points.size() == 0)
      {
        RCLCPP_INFO(LOGGER, "Plan did not succeed");
        continue;
      }
      executionSuccessful = panda_1_arm.execute(my_plan) == moveit::core::MoveItErrorCode::SUCCESS;
    }
  }
  //if()
  //pnp_1->open_gripper();
  tray->next();

  return true;
}

bool executeTrajectory(std::shared_ptr<primitive_pick_and_place> pnp, moveit_msgs::msg::CollisionObject &object, tray_helper *tray)
{
  static int thread_local pregrasp_planning_retries = 0;
  static int thread_local pregrasp_executing_retries = 0;
  static int thread_local grasp_planning_retries = 0;
  static int thread_local grasp_executing_retries = 0;

  RCLCPP_INFO(LOGGER, "Start execution of Object: %s", object.id.c_str());
  geometry_msgs::msg::Pose pose;
  pnp->open_gripper();

  std::this_thread::sleep_for(0.2s);

  // Pre Grasp
  pose.position.x = object.pose.position.x;
  pose.position.y = object.pose.position.y;
  pose.position.z = object.pose.position.z + 0.25;

  pose.orientation.x = object.pose.orientation.w;
  pose.orientation.y = object.pose.orientation.z;
  pose.orientation.z = 0;
  pose.orientation.w = 0;

  while (!(pnp->set_joint_values_from_pose(pose) && pnp->generate_plan() && pnp->execute()))
  {
    RCLCPP_INFO(LOGGER, "Try again pre grasp failed");
    if (!pnp->is_plan_successful())
    {
      RCLCPP_ERROR(LOGGER, "Pre grasp planner failed");
      if(pregrasp_planning_retries >= 2)
      {
        pregrasp_planning_retries = 0;
        return false;
      }
      else
      {
        pregrasp_planning_retries++;
        RCLCPP_ERROR(LOGGER, "Retrying pregrasp planning");
      }
    }
    if(!pnp->is_execution_successful())
    {
      RCLCPP_ERROR(LOGGER, "Pre grasp execution failed");
      if(pregrasp_executing_retries >= 2)
      {
        pregrasp_executing_retries = 0;
        return false;
      }
      else
      {
        pregrasp_executing_retries++;
        RCLCPP_ERROR(LOGGER, "Retrying pregrasp execution");
      }
    }
  }

  pnp->set_default();
  
  // Grasp
  pose.position.z = object.pose.position.z + 0.1;

  while (!(pnp->set_joint_values_from_pose(pose) && pnp->generate_plan() && pnp->execute()))
  {
    RCLCPP_INFO(LOGGER, "Try again grasp failed");
    if (!pnp->is_plan_successful())
    {
      RCLCPP_ERROR(LOGGER, "Grasp planner failed");
      if(grasp_planning_retries >= 2)
      {
        grasp_planning_retries = 0;
        return false;
      }
      else
      {
        grasp_planning_retries++;
        RCLCPP_ERROR(LOGGER, "Retrying grasp planning");
      }
    }
    if(!pnp->is_execution_successful())
    {
      RCLCPP_ERROR(LOGGER, "Grasp execution failed");
      if(grasp_executing_retries >= 2)
      {
        grasp_executing_retries = 0;
        return false;
      }
      else
      {
        grasp_executing_retries++;
        RCLCPP_ERROR(LOGGER, "Retrying grasp executing");
      }
    }
  }

  pnp->set_default();
  
  pnp->grasp_object(object);
  RCLCPP_INFO(LOGGER, "Grasping object with ID %s", object.id.c_str());

  // Once grasped, no turning back! From now, ensure execution with while

  // Pre Move
  pose.position.z = object.pose.position.z + 0.25;

  while (!(pnp->set_joint_values_from_pose(pose) && pnp->generate_plan() && pnp->execute()))
  {
    RCLCPP_INFO(LOGGER, "Try again pre move failed");
    if(!pnp->is_plan_successful())
    {
      RCLCPP_INFO(LOGGER, "Pre move planning failed +++++");
    }
    if(!pnp->is_execution_successful())
    {
      RCLCPP_INFO(LOGGER, "Pre move execution failed ------"); 
    }
  }

  // Move
  pose.position.x = tray->get_x();
  pose.position.y = tray->get_y();
  pose.position.z = 1.28 + tray->z * 0.05;

  pose.orientation.x = 1;
  pose.orientation.y = 0;

  while (!(pnp->set_joint_values_from_pose(pose) && pnp->generate_plan() && pnp->execute()))
  {
    RCLCPP_INFO(LOGGER, "Try again move failed");
  }
  // Put down
  pose.position.z = 1.141 + tray->z * 0.05;

  while (!(pnp->set_joint_values_from_pose(pose) && pnp->generate_plan() && pnp->execute()))
  {
    RCLCPP_INFO(LOGGER, "Try again put down failed");
  }
  pnp->release_object(object);
  // Post Move
  pose.position.z = 1.28 + tray->z * 0.05;

  while (!(pnp->set_joint_values_from_pose(pose) && pnp->generate_plan() && pnp->execute()))
  {
    RCLCPP_INFO(LOGGER, "Try again post move failed");
  }
  tray->next();

  return true;
}
