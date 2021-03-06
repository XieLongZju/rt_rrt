/*
 * manipulator.cpp
 *
 *  Created on: 2017年12月6日
 *      Author: xielong
 */

#include <iostream>
#include <math.h>
//#include <stack>
#include <eigen3/Eigen/Geometry>

#include "../header/Manipulator.h"

Manipulator::Manipulator(Eigen::MatrixXd dh_param){
	Eigen::MatrixXd q_init = Eigen::MatrixXd::Zero(7,1);
	setJointAngle(q_init);
	setDhParam(dh_param);

	Manipulator::goal_bais = 0.1;
	Manipulator::link_num = 7;
	Manipulator::max_iter = 10e3;
	Manipulator::step_div = 2;
	Manipulator::obstacle_num = 3;
	Manipulator::node_max_step = 0.0462; // sqrt(sum(((1 * pi / 180)*ones(7, 1)).^2))

	Manipulator::max_ang = 130*M_PI/180 * Eigen::MatrixXd::Ones(Manipulator::link_num, 1);
	Manipulator::min_ang = -45*M_PI/180 * Eigen::MatrixXd::Ones(Manipulator::link_num, 1);
	Manipulator::arm_radius = 0.04;

	Manipulator::tree = Eigen::MatrixXd::Zero(Manipulator::link_num, Manipulator::max_iter);
	Manipulator::parent = Eigen::MatrixXd::Zero(1, Manipulator::max_iter);
	Manipulator::children = Eigen::MatrixXd::Zero(1, Manipulator::max_iter);
	Manipulator::sum_cost = Eigen::MatrixXd::Zero(1, Manipulator::max_iter);
}

void Manipulator::setJointAngle(Eigen::MatrixXd joint_angle){
	Manipulator::joint_angle = joint_angle;
}

void Manipulator::setDhParam(Eigen::MatrixXd dh_param){
	Manipulator::dh_param = dh_param;
}

Eigen::MatrixXd Manipulator::getDhParam(){
	return Manipulator::dh_param;
}

void Manipulator::setGoalPosition(Eigen::MatrixXd goal_position){
	Manipulator::goal_position = goal_position;
	Manipulator::goal_angle = Manipulator::ikine(goal_position);
}

void Manipulator::setStartState(Eigen::MatrixXd joint_angle){
	Manipulator::start_angle = joint_angle;
	Manipulator::tree.col(0) = joint_angle;
	Manipulator::node_added = 1;
}

Eigen::MatrixXd Manipulator::jacob(Eigen::MatrixXd joint_angle){
	Eigen::MatrixXd Jn = Eigen::MatrixXd::Zero(6, 7);
	Eigen::MatrixXd J0 = Eigen::MatrixXd::Zero(6, 7);
	Eigen::MatrixXd J_temp = Eigen::MatrixXd::Zero(6, 6);
	Eigen::MatrixXd UT = Eigen::MatrixXd::Identity(4, 4);
	Eigen::MatrixXd dh = Manipulator::getDhParam();
	Eigen::Matrix4Xd T_temp(4,4);
	Eigen::MatrixXd delta(3, 1);
	Eigen::MatrixXd D(3, 1);
	for(int i = 6; i >= 0; i--){
		float q = joint_angle(i);
		float d = dh(i,1);
		float a = dh(i,2);
		float alpha = dh(i,3);
		T_temp << cos(q), -sin(q)*cos(alpha), sin(q)*sin(alpha), a*cos(q),
			      sin(q),  cos(q)*cos(alpha),-cos(q)*sin(alpha), a*sin(q),
					0,         sin(alpha),         cos(alpha),       d,
					0,            0,                 0,              1;
		UT = T_temp*UT;
		D << -UT(0,0)*UT(1,3)+UT(1,0)*UT(0,3),
		     -UT(0,1)*UT(1,3)+UT(1,1)*UT(0,3),
		     -UT(0,2)*UT(1,3)+UT(1,2)*UT(0,3);
		delta = UT.block(2,0,1,3).transpose();
		Jn.block(0,i,3,1) = D;
		Jn.block(3,i,3,1) = delta;
	}
	J_temp.block(0,0,3,3) = UT.block(0,0,3,3);
	J_temp.block(3,3,3,3) = UT.block(0,0,3,3);
	J0 = J_temp * Jn;
	return J0;
}

Eigen::MatrixXd Manipulator::fkine(Eigen::MatrixXd joint_angle){
	Eigen::MatrixXd joint_position(3,4);
	joint_position.col(0) << 0, 0, 0.20386;
	Eigen::MatrixXd dh = Manipulator::getDhParam();
	Eigen::Matrix4Xd T = Eigen::Matrix4Xd::Identity(4,4);
	for(int i = 0; i < joint_angle.rows(); i++){
		float q = joint_angle(i);
		float d = dh(i,1);
		float a = dh(i,2);
		float alpha = dh(i,3);
		Eigen::Matrix4Xd T_temp(4,4);
		T_temp << cos(q), -sin(q)*cos(alpha), sin(q)*sin(alpha), a*cos(q),
			      sin(q),  cos(q)*cos(alpha),-cos(q)*sin(alpha), a*sin(q),
			        0,         sin(alpha),         cos(alpha),       d,
			        0,            0,                 0,              1;
		T = T*T_temp;
		if(i == 3){
			joint_position.col(1) = T.block(0,3,3,1);
		}
		if(i == 5){
			joint_position.col(2) = T.block(0,3,3,1);
		}
	}
	joint_position.col(3) = T.block(0,3,3,1);
	return joint_position;
}

Eigen::MatrixXd Manipulator::ikine(Eigen::MatrixXd goal_position){
	float lamda = 0.2;           // damping constant
	float stol = 1e-3;           // tolerance
	float nm_error = 100;        // initial error
	int count = 0;             // iteration count
	int ilimit = 1000;         // maximum iteration
	Eigen::MatrixXd end_position;
	Eigen::MatrixXd error;
	Eigen::MatrixXd jacob;
	Eigen::MatrixXd jacob_t;
	Eigen::MatrixXd A;
	Eigen::MatrixXd B = Eigen::MatrixXd::Zero(6, 1);
	Eigen::MatrixXd I = Eigen::MatrixXd::Identity(6, 6);
	Eigen::MatrixXd f;

	Eigen::MatrixXd joint_angle(7,1);
	joint_angle << 0, 0, 0, M_PI/2, 0, M_PI/2, 0;
	while(nm_error > stol){
		end_position = Manipulator::fkine(joint_angle).col(3);
		error = goal_position - end_position;
		B.block(0,0,3,1) = error;
		jacob = Manipulator::jacob(joint_angle);
		jacob_t = jacob.transpose();
		A = jacob*jacob_t + lamda*lamda * I;
		f = A.lu().solve(B);
		joint_angle = joint_angle + jacob_t * f;
		nm_error = error.norm();
		count += 1;
		if(count > ilimit){
			std::cout<< "Solution wouldn't converge" << std::endl;
			break;
		}
	}
	return joint_angle;
}

Eigen::Matrix4Xd Manipulator::transMatrix (Eigen::MatrixXd link, float q){
	float d = link(1);
	float a = link(2);
	float alpha = link(3);
	Eigen::Matrix4Xd result(4,4);
	result << cos(q), -sin(q)*cos(alpha), sin(q)*sin(alpha), a*cos(q),
		 sin(q),  cos(q)*cos(alpha),-cos(q)*sin(alpha), a*sin(q),
		   0,         sin(alpha),         cos(alpha),       d,
		   0,            0,                 0,              1;
	return result;
}

Eigen::MatrixXd Manipulator::sampleNode(){
	Eigen::MatrixXd state;
	if((double)rand()/RAND_MAX < Manipulator::goal_bais){
		state = Manipulator::goal_angle + Eigen::MatrixXd::Random(Manipulator::link_num, 1) * Manipulator::node_max_step;
	}else{
		Eigen::MatrixXd step = Manipulator::max_ang - Manipulator::min_ang;
		Eigen::MatrixXd random = (Eigen::MatrixXd::Random(Manipulator::link_num, 1)
								+ Eigen::MatrixXd::Ones(Manipulator::link_num, 1)) / 2;
		state = step.cwiseProduct(random) +  Manipulator::min_ang;
	}
	return state;
}

void Manipulator::getNearestNode(Eigen::MatrixXd node, NearestNode* nearest_node){
	float dist = 65536;
	int index = 0;
	for(int i = 0; i < Manipulator::node_added; i++){
		float dist_temp = (Manipulator::tree.col(i) - node).norm();
		if(dist > dist_temp){
			dist = dist_temp;
			index = i;
		}
	}
	nearest_node->ind = index;
	nearest_node->nearest_dist = dist;
}

NearestNode Manipulator::getNearestNode(Eigen::MatrixXd node, int* node_added){
	NearestNode nearest_node;
	Eigen::MatrixXd::Index maxCol;
	Eigen::MatrixXd::Index maxRow;
	Eigen::MatrixXd dist = (Manipulator::tree - node.replicate(1, *node_added)).colwise().norm();
	float s = dist.minCoeff(&maxRow, &maxCol);
	nearest_node.ind = maxCol;
	nearest_node.nearest_dist = sqrt(s);
	return nearest_node;
}

Eigen::MatrixXd Manipulator::getNeighbors(Eigen::MatrixXd new_node, int nearest_node_ind){
	Eigen::MatrixXd from;
	return from;
}

Eigen::MatrixXd Manipulator::steer(Eigen::MatrixXd new_node, int nearest_node_ind){
	Eigen::MatrixXd from = Manipulator::tree.col(nearest_node_ind);
	Eigen::MatrixXd angle_diff = new_node - from;
	Eigen::MatrixXd steer_node = from + angle_diff / angle_diff.norm() * Manipulator::node_max_step;
	return steer_node;
}

//int Manipulator::obstacleCollision(Eigen::MatrixXd new_node, int nearest_node_ind, Eigen::MatrixXd obs_position){
//	Eigen::MatrixXd nearest_node = Manipulator::tree.col(nearest_node_ind);
//	Eigen::MatrixXd dist_temp = new_node - nearest_node;
//	double dist = dist_temp.norm();
//	Eigen::MatrixXd vector = dist_temp/dist;
//	double step = dist / Manipulator::step_div;
//
//	Eigen::MatrixXd state_angle;
//	Eigen::MatrixXd joint_position;
//	Eigen::MatrixXd ob_dist(Manipulator::obstacle_num, 1);
//
//	Eigen::MatrixXd link_dist;
//	Eigen::MatrixXd obs_dist;
//	int collision;
//	for(int i = 0; i < Manipulator::obstacle_num; ++i){
//		for(int j = 1; j <= Manipulator::step_div; ++j){
//			state_angle = nearest_node + j*step*vector;
//			joint_position = Manipulator::fkine(state_angle);
//			ob_dist(0,0) = Manipulator::linkObstacleCollision(joint_position.col(0), joint_position.col(1), obs_position.col(i));
//			ob_dist(1,0) = Manipulator::linkObstacleCollision(joint_position.col(1), joint_position.col(2), obs_position.col(i));
//			ob_dist(2,0) = Manipulator::linkObstacleCollision(joint_position.col(2), joint_position.col(3), obs_position.col(i));
//			collision = (ob_dist.minCoeff() - Manipulator::arm_radius - Manipulator::obs_radius[i]) > 0;
//			if(!collision){
//				return collision;
//			}
//		}
//	}
//	return 1;
//}

int Manipulator::obstacleCollision(Eigen::MatrixXd new_node, int nearest_node_ind, Eigen::MatrixXd obs_position){
	Eigen::MatrixXd nearest_node = Manipulator::tree.col(nearest_node_ind);
	Eigen::MatrixXd dist_temp = new_node - nearest_node;
	double dist = dist_temp.norm();
	Eigen::MatrixXd vector = dist_temp/dist;
	double step = dist / Manipulator::step_div;

	Eigen::MatrixXd state_angle;
	Eigen::MatrixXd joint_position;

	Eigen::MatrixXd obs_dist;
	for(int i = 0; i < Manipulator::obstacle_num; ++i){
		for(int j = 1; j <= Manipulator::step_div; ++j){
			state_angle = nearest_node + j*step*vector;
			joint_position = Manipulator::fkine(state_angle);
			obs_dist = (joint_position - obs_position.col(i).replicate(1, 4)).colwise().norm();
			float dist = 65536;
			for(int k = 0; k < 3; ++k){
				if(obs_dist(0, k) + obs_dist(0, k+1) == Manipulator::link_length[k]){
					return 0;
				}else if(pow(obs_dist(0, k+1), 2) >= pow(obs_dist(0, k), 2) + pow(Manipulator::link_length[k], 2)){
					if(obs_dist(0, k) < dist){
						dist = obs_dist(0, k);
					}
				}else if(pow(obs_dist(0, k), 2) >= pow(obs_dist(0, k+1), 2) + pow(Manipulator::link_length[k], 2)){
					if(obs_dist(0, k+1) < dist){
						dist = obs_dist(0, k+1);
					}
				}else{
					float p = (obs_dist(0, k) + obs_dist(0, k+1) + Manipulator::link_length[k])/2;
					float S = sqrt(p*(p - obs_dist(0, k))*(p - obs_dist(0, k+1))*(p - Manipulator::link_length[k]));
					float h = 2*S/Manipulator::link_length[k];
					if(h < dist){
						dist = h;
					}
				}
			}
			dist = dist - Manipulator::arm_radius - Manipulator::obs_radius[i];
			if(dist < 0){
				return 0;
			}
		}
	}
	return 1;
}

int Manipulator::obstacleCollision(Eigen::MatrixXd new_node, Eigen::MatrixXd nearest_node, Eigen::MatrixXd obs_position){
	Eigen::MatrixXd dist_temp = new_node - nearest_node;
	double dist = dist_temp.norm();
	Eigen::MatrixXd vector = dist_temp/dist;
	double step = dist / Manipulator::step_div;

	Eigen::MatrixXd state_angle;
	Eigen::MatrixXd joint_position;

	Eigen::MatrixXd obs_dist;
	for(int i = 0; i < Manipulator::obstacle_num; ++i){
		for(int j = 1; j <= Manipulator::step_div; ++j){
			state_angle = nearest_node + j*step*vector;
			joint_position = Manipulator::fkine(state_angle);
			obs_dist = (joint_position - obs_position.col(i).replicate(1, 4)).colwise().norm();
			float dist = 65536;
			for(int k = 0; k < 3; ++k){
				if(obs_dist(0, k) + obs_dist(0, k+1) == Manipulator::link_length[k]){
					return 0;
				}else if(pow(obs_dist(0, k+1), 2) >= pow(obs_dist(0, k), 2) + pow(Manipulator::link_length[k], 2)){
					if(obs_dist(0, k) < dist){
						dist = obs_dist(0, k);
					}
				}else if(pow(obs_dist(0, k), 2) >= pow(obs_dist(0, k+1), 2) + pow(Manipulator::link_length[k], 2)){
					if(obs_dist(0, k+1) < dist){
						dist = obs_dist(0, k+1);
					}
				}else{
					float p = (obs_dist(0, k) + obs_dist(0, k+1) + Manipulator::link_length[k])/2;
					float S = sqrt(p*(p - obs_dist(0, k))*(p - obs_dist(0, k+1))*(p - Manipulator::link_length[k]));
					float h = 2*S/Manipulator::link_length[k];
					if(h < dist){
						dist = h;
					}
				}
			}
			dist = dist - Manipulator::arm_radius - Manipulator::obs_radius[i];
			if(dist < 0){
				return 0;
			}
		}
	}
	return 1;
}

//float Manipulator::linkObstacleCollision(Eigen::MatrixXd P1, Eigen::MatrixXd P2, Eigen::MatrixXd obstacle){
//	Eigen::MatrixXd a1 = obstacle - P1;
//	Eigen::MatrixXd a2 = P2 - P1;
//	Eigen::MatrixXd close_P(3, 1);
//	float k = (a1.transpose() * a2)(0, 0) / a2.norm();//(a2.transpose() * a2)(0, 0);
//	if(k <= 0){
//		close_P = P1;
//	}else if(k >= 1){
//		close_P = P2;
//	}else{
//		close_P = P1 + k * a2;
//	}
//	float ob_dist = (close_P - obstacle).norm();
//	return ob_dist;
//}

int Manipulator::insertNode(Eigen::MatrixXd new_node, int nearest_node_ind){
//	int node_added = Manipulator::node_added;
	Manipulator::tree.col(Manipulator::node_added) = new_node;
	Manipulator::parent(0, Manipulator::node_added) = nearest_node_ind;
	Manipulator::children(0, nearest_node_ind) += 1;
	Manipulator::sum_cost(0, Manipulator::node_added) = Manipulator::sum_cost(0, nearest_node_ind)
											+ (new_node - Manipulator::tree.col(nearest_node_ind)).norm();
	Manipulator::node_added = Manipulator::node_added + 1;
	return Manipulator::node_added - 1;
}

void Manipulator::findPath(int nearest_node_index){
	int current_index = nearest_node_index;
	int path_iter = 0;
	while(current_index != 0 ){
		Manipulator::back_trace.push(current_index);
		path_iter = path_iter + 1;
		current_index = Manipulator::parent(current_index, 0);
	}
	Manipulator::back_trace.push(current_index);
}

