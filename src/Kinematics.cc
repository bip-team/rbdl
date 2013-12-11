/*
 * RBDL - Rigid Body Dynamics Library
 * Copyright (c) 2011-2012 Martin Felis <martin.felis@iwr.uni-heidelberg.de>
 *
 * Licensed under the zlib license. See LICENSE for more details.
 */

#include <iostream>
#include <limits>
#include <cstring>
#include <assert.h>

#include "rbdl/rbdl_mathutils.h"
#include "rbdl/Logging.h"

#include "rbdl/Model.h"
#include "rbdl/Kinematics.h"

namespace RigidBodyDynamics {

using namespace Math;

RBDL_DLLAPI
void UpdateKinematics (Model &model,
		const VectorNd &Q,
		const VectorNd &QDot,
		const VectorNd &QDDot
		) {
	LOG << "-------- " << __func__ << " --------" << std::endl;

	unsigned int i;

	SpatialVector spatial_gravity (0., 0., 0., model.gravity[0], model.gravity[1], model.gravity[2]);

	model.a[0].setZero();
	//model.a[0] = spatial_gravity;

	for (i = 1; i < model.mBodies.size(); i++) {
		unsigned int q_index = model.mJoints[i].q_index;

		SpatialTransform X_J;
		SpatialMotion v_J;
		SpatialMotion c_J;
		Joint joint = model.mJoints[i];
		unsigned int lambda = model.lambda[i];

		jcalc (model, i, X_J, v_J, c_J, Q, QDot);

		model.X_lambda[i] = X_J * model.X_T[i];

		if (lambda != 0) {
			model.X_base[i] = model.X_lambda[i] * model.X_base.at(lambda);
			model.v[i] = model.X_lambda[i].apply(model.v[lambda]) + v_J;
			model.c[i] = c_J + model.v[i].cross(v_J);
		}	else {
			model.X_base[i] = model.X_lambda[i];
			model.v[i] = v_J;
			model.c[i].setZero();
		}
		
		model.a[i] = model.X_lambda[i].apply(model.a[lambda]) + model.c[i];

		if (model.mJoints[i].mDoFCount == 3) {
			Vector3d omegadot_temp (QDDot[q_index], QDDot[q_index + 1], QDDot[q_index + 2]);
			model.a[i] = model.a[i] + SpatialMotion::fromVector(model.multdof3_S[i] * omegadot_temp);
		} else {
			model.a[i] = model.a[i] + model.S[i] * QDDot[q_index];
		}	
	}

	for (i = 1; i < model.mBodies.size(); i++) {
		LOG << "a[" << i << "] = " << model.a[i] << std::endl;
	}
}

RBDL_DLLAPI
void UpdateKinematicsCustom (Model &model,
		const VectorNd *Q,
		const VectorNd *QDot,
		const VectorNd *QDDot
		) {
	LOG << "-------- " << __func__ << " --------" << std::endl;
	
	unsigned int i;

	if (Q) {
		for (i = 1; i < model.mBodies.size(); i++) {
			unsigned int q_index = model.mJoints[i].q_index;
			SpatialMotion v_J;
			SpatialMotion c_J;
			SpatialTransform X_J;
			Joint joint = model.mJoints[i];
			unsigned int lambda = model.lambda[i];

			VectorNd QDot_zero (VectorNd::Zero (model.q_size));

			jcalc (model, i, X_J, v_J, c_J, (*Q), QDot_zero);

			model.X_lambda[i] = X_J * model.X_T[i];

			if (lambda != 0) {
				model.X_base[i] = model.X_lambda[i] * model.X_base.at(lambda);
			}	else {
				model.X_base[i] = model.X_lambda[i];
			}
		}
	}

	if (QDot) {
		for (i = 1; i < model.mBodies.size(); i++) {
			unsigned int q_index = model.mJoints[i].q_index;
			SpatialMotion v_J;
			SpatialMotion c_J;
			SpatialTransform X_J;
			Joint joint = model.mJoints[i];
			unsigned int lambda = model.lambda[i];

			jcalc (model, i, X_J, v_J, c_J, *Q, *QDot);

			if (lambda != 0) {
				model.v[i] = model.X_lambda[i].apply(model.v[lambda]) + v_J;
				model.c[i] = c_J + model.v[i].cross(v_J);
			}	else {
				model.v[i] = v_J;
				model.c[i].setZero();
			}
			// LOG << "v[" << i << "] = " << model.v[i].transpose() << std::endl;
		}
	}

	if (QDDot) {
		for (i = 1; i < model.mBodies.size(); i++) {
			unsigned int q_index = model.mJoints[i].q_index;

			unsigned int lambda = model.lambda[i];

			if (lambda != 0) {
				model.a[i] = model.X_lambda[i].apply(model.a[lambda]) + model.c[i];
			}	else {
				model.a[i].setZero();
			}

			if (model.mJoints[i].mDoFCount == 3) {
				Vector3d omegadot_temp ((*QDDot)[q_index], (*QDDot)[q_index + 1], (*QDDot)[q_index + 2]);
				model.a[i] = model.a[i] + SpatialMotion::fromVector(model.multdof3_S[i] * omegadot_temp);
			} else {
				model.a[i] = model.a[i] + model.S[i] * (*QDDot)[q_index];
			}
		}
	}
}

RBDL_DLLAPI
Vector3d CalcBodyToBaseCoordinates (
		Model &model,
		const VectorNd &Q,
		unsigned int body_id,
		const Vector3d &point_body_coordinates,
		bool update_kinematics) {
	// update the Kinematics if necessary
	if (update_kinematics) {
		UpdateKinematicsCustom (model, &Q, NULL, NULL);
	}

	if (body_id >= model.fixed_body_discriminator) {
		unsigned int fbody_id = body_id - model.fixed_body_discriminator;
		unsigned int parent_id = model.mFixedBodies[fbody_id].mMovableParent;

		Matrix3d fixed_rotation = model.mFixedBodies[fbody_id].mParentTransform.E.transpose();
		Vector3d fixed_position = model.mFixedBodies[fbody_id].mParentTransform.r;

		Matrix3d parent_body_rotation = model.X_base[parent_id].E.transpose();
		Vector3d parent_body_position = model.X_base[parent_id].r;
		return parent_body_position + parent_body_rotation * (fixed_position + fixed_rotation * (point_body_coordinates));
	}

	Matrix3d body_rotation = model.X_base[body_id].E.transpose();
	Vector3d body_position = model.X_base[body_id].r;

	return body_position + body_rotation * point_body_coordinates;
}

RBDL_DLLAPI
Vector3d CalcBaseToBodyCoordinates (
		Model &model,
		const VectorNd &Q,
		unsigned int body_id,
		const Vector3d &point_base_coordinates,
		bool update_kinematics) {
	if (update_kinematics) {
		UpdateKinematicsCustom (model, &Q, NULL, NULL);
	}

	if (body_id >= model.fixed_body_discriminator) {
		unsigned int fbody_id = body_id - model.fixed_body_discriminator;
		unsigned int parent_id = model.mFixedBodies[fbody_id].mMovableParent;

		Matrix3d fixed_rotation = model.mFixedBodies[fbody_id].mParentTransform.E;
		Vector3d fixed_position = model.mFixedBodies[fbody_id].mParentTransform.r;

		Matrix3d parent_body_rotation = model.X_base[parent_id].E;
		Vector3d parent_body_position = model.X_base[parent_id].r;

		return fixed_rotation * ( - fixed_position - parent_body_rotation * (parent_body_position - point_base_coordinates));
	}

	Matrix3d body_rotation = model.X_base[body_id].E;
	Vector3d body_position = model.X_base[body_id].r;

	return body_rotation * (point_base_coordinates - body_position);
}

RBDL_DLLAPI
Matrix3d CalcBodyWorldOrientation (
		Model &model,
		const VectorNd &Q,
		const unsigned int body_id,
		bool update_kinematics) 
{
	// update the Kinematics if necessary
	if (update_kinematics) {
		UpdateKinematicsCustom (model, &Q, NULL, NULL);
	}

	if (body_id >= model.fixed_body_discriminator) {
		unsigned int fbody_id = body_id - model.fixed_body_discriminator;
		model.mFixedBodies[fbody_id].mBaseTransform = model.X_base[model.mFixedBodies[fbody_id].mMovableParent] * model.mFixedBodies[fbody_id].mParentTransform;

		return model.mFixedBodies[fbody_id].mBaseTransform.E;
	}

	return model.X_base[body_id].E;
}

RBDL_DLLAPI
void CalcPointJacobian (
		Model &model,
		const VectorNd &Q,
		unsigned int body_id,
		const Vector3d &point_position,
		MatrixNd &G,
		bool update_kinematics
	) {
	LOG << "-------- " << __func__ << " --------" << std::endl;

	// update the Kinematics if necessary
	if (update_kinematics) {
		UpdateKinematicsCustom (model, &Q, NULL, NULL);
	}

	Vector3d point_base_pos = CalcBodyToBaseCoordinates (model, Q, body_id, point_position, false);
	SpatialTransform point_trans (Matrix3d::Identity(), point_base_pos);

	assert (G.rows() == 3 && G.cols() == model.qdot_size );

	G.setZero();

	// we have to make sure that only the joints that contribute to the
	// bodies motion also get non-zero columns in the jacobian.
	// VectorNd e = VectorNd::Zero(Q.size() + 1);
	char *e = new char[Q.size() + 1];
	if (e == NULL) {
		std::cerr << "Error: allocating memory." << std::endl;
		abort();
	}
	memset (&e[0], 0, Q.size() + 1);

	unsigned int reference_body_id = body_id;

	if (model.IsFixedBodyId(body_id)) {
		unsigned int fbody_id = body_id - model.fixed_body_discriminator;
		reference_body_id = model.mFixedBodies[fbody_id].mMovableParent;
	}

	unsigned int j = reference_body_id;

	// e[j] is set to 1 if joint j contributes to the jacobian that we are
	// computing. For all other joints the column will be zero.
	while (j != 0) {
		e[j] = 1;
		j = model.lambda[j];
	}

	for (j = 1; j < model.mBodies.size(); j++) {
		if (e[j] == 1) {
			unsigned int q_index = model.mJoints[j].q_index;

			if (model.mJoints[j].mDoFCount == 3) {
				Matrix63 S_base = point_trans.toMatrix() * model.X_base[j].inverse().toMatrix() * model.multdof3_S[j];

				G(0, q_index) = S_base(3, 0);
				G(1, q_index) = S_base(4, 0);
				G(2, q_index) = S_base(5, 0);

				G(0, q_index + 1) = S_base(3, 1);
				G(1, q_index + 1) = S_base(4, 1);
				G(2, q_index + 1) = S_base(5, 1);

				G(0, q_index + 2) = S_base(3, 2);
				G(1, q_index + 2) = S_base(4, 2);
				G(2, q_index + 2) = S_base(5, 2);
			} else {
				SpatialMotion S_base = point_trans.apply(model.X_base[j].applyInverse(model.S[j]));

				G(0, q_index) = S_base.v[0];
				G(1, q_index) = S_base.v[1];
				G(2, q_index) = S_base.v[2];
			}
		}
	}
	
	delete[] e;
}

RBDL_DLLAPI
Vector3d CalcPointVelocity (
		Model &model,
		const VectorNd &Q,
		const VectorNd &QDot,
		unsigned int body_id,
		const Vector3d &point_position,
		bool update_kinematics
	) {
	LOG << "-------- " << __func__ << " --------" << std::endl;
	assert (model.IsBodyId(body_id));
	assert (model.q_size == Q.size());
	assert (model.qdot_size == QDot.size());

	// Reset the velocity of the root body
	model.v[0].setZero();

	// update the Kinematics with zero acceleration
	if (update_kinematics) {
		UpdateKinematicsCustom (model, &Q, &QDot, NULL);
	}

	Vector3d point_abs_pos = CalcBodyToBaseCoordinates (model, Q, body_id, point_position, false); 

	unsigned int reference_body_id = body_id;

	if (model.IsFixedBodyId(body_id)) {
		unsigned int fbody_id = body_id - model.fixed_body_discriminator;
		reference_body_id = model.mFixedBodies[fbody_id].mMovableParent;
	}

	LOG << "body_index     = " << body_id << std::endl;
	LOG << "point_pos      = " << point_position.transpose() << std::endl;
//	LOG << "global_velo    = " << global_velocities.at(body_id) << std::endl;
	LOG << "body_transf    = " << std::endl << model.X_base[reference_body_id].toMatrix() << std::endl;
	LOG << "point_abs_ps   = " << point_abs_pos.transpose() << std::endl;
	LOG << "X   = " << std::endl << (SpatialTransform (Matrix3d::Identity(3,3), point_abs_pos) * model.X_base[reference_body_id].inverse()).toMatrix() << std::endl;
	LOG << "v   = " << model.v[reference_body_id] << std::endl;

	// Now we can compute the spatial velocity at the given point
//	SpatialVector body_global_velocity (global_velocities.at(body_id));
	SpatialMotion point_spatial_velocity = (SpatialTransform (Matrix3d::Identity(3,3), point_abs_pos) * model.X_base[reference_body_id].inverse()).apply(model.v[reference_body_id]);

	LOG << "point_velocity = " <<	point_spatial_velocity.v.transpose() << std::endl;

	return point_spatial_velocity.v;
}

RBDL_DLLAPI
Vector3d CalcPointAcceleration (
		Model &model,
		const VectorNd &Q,
		const VectorNd &QDot,
		const VectorNd &QDDot,
		unsigned int body_id,
		const Vector3d &point_position,
		bool update_kinematics
	)
{
	LOG << "-------- " << __func__ << " --------" << std::endl;

	// Reset the velocity of the root body
	model.v[0].setZero();
	model.a[0].setZero();

	if (update_kinematics)
		UpdateKinematics (model, Q, QDot, QDDot);

	LOG << std::endl;

	unsigned int reference_body_id = body_id;
	Vector3d reference_point = point_position;

	if (model.IsFixedBodyId(body_id)) {
		unsigned int fbody_id = body_id - model.fixed_body_discriminator;
		reference_body_id = model.mFixedBodies[fbody_id].mMovableParent;
		Vector3d base_coords = CalcBodyToBaseCoordinates (model, Q, body_id, point_position, false);
		reference_point = CalcBaseToBodyCoordinates (model, Q, reference_body_id, base_coords, false);
	}

	SpatialMotion body_global_velocity (model.X_base[reference_body_id].applyInverse(model.v[reference_body_id]));

	LOG << " orientation " << std::endl << CalcBodyWorldOrientation (model, Q, reference_body_id, false) << std::endl;
	LOG << " orientationT " << std::endl <<  CalcBodyWorldOrientation (model, Q, reference_body_id, false).transpose() << std::endl;

	Matrix3d global_body_orientation_inv = CalcBodyWorldOrientation (model, Q, reference_body_id, false).transpose();

	SpatialTransform p_X_i (global_body_orientation_inv, reference_point);

	LOG << "p_X_i = " << std::endl << p_X_i << std::endl;

	SpatialMotion p_v_i = p_X_i.apply(model.v[reference_body_id]);
	SpatialMotion p_a_i = p_X_i.apply(model.a[reference_body_id]);

	SpatialMotion frame_acceleration = SpatialMotion(Vector3d(0., 0., 0.), p_v_i.v).cross (body_global_velocity);

	LOG << "v_i                = " << model.v[reference_body_id] << std::endl;
	LOG << "a_i                = " << model.a[reference_body_id] << std::endl;
	LOG << "p_X_i              = " << std::endl << p_X_i << std::endl;
	LOG << "p_v_i              = " << p_v_i << std::endl;
	LOG << "p_a_i              = " << p_a_i << std::endl;
	LOG << "body_global_vel    = " << body_global_velocity << std::endl;
	LOG << "frame_acceleration = " << frame_acceleration << std::endl;

	SpatialMotion p_a_i_dash = p_a_i - frame_acceleration;

	LOG << "point_acceleration = " <<	p_a_i_dash.v.transpose() << std::endl;

	return p_a_i_dash.v;
}

RBDL_DLLAPI 
bool InverseKinematics (
		Model &model,
		const VectorNd &Qinit,
		const std::vector<unsigned int>& body_id,
		const std::vector<Vector3d>& body_point,
		const std::vector<Vector3d>& target_pos,
		VectorNd &Qres,
		double step_tol,
		double lambda,
		unsigned int max_iter
		) {

	assert (Qinit.size() == model.q_size);
	assert (body_id.size() == body_point.size());
	assert (body_id.size() == target_pos.size());

	MatrixNd J = MatrixNd::Zero(3 * body_id.size(), model.qdot_size);
	VectorNd e = VectorNd::Zero(3 * body_id.size());

	Qres = Qinit;

	for (unsigned int ik_iter = 0; ik_iter < max_iter; ik_iter++) {
		UpdateKinematicsCustom (model, &Qres, NULL, NULL);

		for (unsigned int k = 0; k < body_id.size(); k++) {
			MatrixNd G (3, model.qdot_size);
			CalcPointJacobian (model, Qres, body_id[k], body_point[k], G, false);
			Vector3d point_base = CalcBodyToBaseCoordinates (model, Qres, body_id[k], body_point[k], false);
			LOG << "current_pos = " << point_base.transpose() << std::endl;

			for (unsigned int i = 0; i < 3; i++) {
				for (unsigned int j = 0; j < model.qdot_size; j++) {
					unsigned int row = k * 3 + i;
					LOG << "i = " << i << " j = " << j << " k = " << k << " row = " << row << " col = " << j << std::endl;
					J(row, j) = G (i,j);
				}

				e[k * 3 + i] = target_pos[k][i] - point_base[i];
			}

			LOG << J << std::endl;

			// abort if we are getting "close"
			if (e.norm() < step_tol) {
				LOG << "Reached target close enough after " << ik_iter << " steps" << std::endl;
				return true;
			}
		}

		LOG << "J = " << J << std::endl;
		LOG << "e = " << e.transpose() << std::endl;

		MatrixNd JJTe_lambda2_I = J * J.transpose() + lambda*lambda * MatrixNd::Identity(e.size(), e.size());

		VectorNd z (body_id.size() * 3);
#ifndef RBDL_USE_SIMPLE_MATH
		z = JJTe_lambda2_I.colPivHouseholderQr().solve (e);
#else
		bool solve_successful = LinSolveGaussElimPivot (JJTe_lambda2_I, e, z);
		assert (solve_successful);
#endif

		LOG << "z = " << z << std::endl;

		VectorNd delta_theta = J.transpose() * z;
		LOG << "change = " << delta_theta << std::endl;

		Qres = Qres + delta_theta;
		LOG << "Qres = " << Qres.transpose() << std::endl;

		if (delta_theta.norm() < step_tol) {
			LOG << "reached convergence after " << ik_iter << " steps" << std::endl;
			return true;
		}

		VectorNd test_1 (z.size());
		VectorNd test_res (z.size());

		test_1.setZero();

		for (unsigned int i = 0; i < z.size(); i++) {
			test_1[i] = 1.;

			VectorNd test_delta = J.transpose() * test_1;

			test_res[i] = test_delta.squaredNorm();

			test_1[i] = 0.;
		}

		LOG << "test_res = " << test_res.transpose() << std::endl;
	}

	return false;
}

}
