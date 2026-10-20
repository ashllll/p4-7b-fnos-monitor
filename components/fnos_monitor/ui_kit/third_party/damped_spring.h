#pragma once
#include <math.h>
/* Altered version: C99 typedef, pointer instead of C++ reference, static inline.
   Numerical implementation unchanged. Source and hash: README.md. */
/******************************************************************************
  Copyright (c) 2008-2012 Ryan Juckett
  http://www.ryanjuckett.com/

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

  3. This notice may not be removed or altered from any source
     distribution.
******************************************************************************/


//******************************************************************************
// Cached set of motion parameters that can be used to efficiently update
// multiple springs using the same time step, angular frequency and damping
// ratio.
//******************************************************************************
typedef struct tDampedSpringMotionParams
{
	// newPos = posPosCoef*oldPos + posVelCoef*oldVel
	float m_posPosCoef, m_posVelCoef;
	// newVel = velPosCoef*oldPos + velVelCoef*oldVel
	float m_velPosCoef, m_velVelCoef;
} tDampedSpringMotionParams;

//******************************************************************************
// This function will compute the parameters needed to simulate a damped spring
// over a given period of time.
// - An angular frequency is given to control how fast the spring oscillates.
// - A damping ratio is given to control how fast the motion decays.
//     damping ratio > 1: over damped
//     damping ratio = 1: critically damped
//     damping ratio < 1: under damped
//******************************************************************************
static inline void CalcDampedSpringMotionParams(
	tDampedSpringMotionParams* pOutParams,       // motion parameters result
	float	                   deltaTime,        // time step to advance
	float	                   angularFrequency, // angular frequency of motion
	float	                   dampingRatio)     // damping ratio of motion
{
	const float epsilon = 0.0001f;

	// force values into legal range
	if (dampingRatio     < 0.0f) dampingRatio     = 0.0f;
	if (angularFrequency < 0.0f) angularFrequency = 0.0f;

	// if there is no angular frequency, the spring will not move and we can
	// return identity
	if ( angularFrequency < epsilon )
	{
		pOutParams->m_posPosCoef = 1.0f; pOutParams->m_posVelCoef = 0.0f;
		pOutParams->m_velPosCoef = 0.0f; pOutParams->m_velVelCoef = 1.0f;
		return;
	}

	if (dampingRatio > 1.0f + epsilon)
	{
		// over-damped
		float za = -angularFrequency * dampingRatio;
		float zb = angularFrequency * sqrtf(dampingRatio*dampingRatio - 1.0f);
		float z1 = za - zb;
		float z2 = za + zb;

		float e1 = expf( z1 * deltaTime );
		float e2 = expf( z2 * deltaTime );

		float invTwoZb = 1.0f / (2.0f*zb); // = 1 / (z2 - z1)

		float e1_Over_TwoZb = e1*invTwoZb;
		float e2_Over_TwoZb = e2*invTwoZb;

		float z1e1_Over_TwoZb = z1*e1_Over_TwoZb;
		float z2e2_Over_TwoZb = z2*e2_Over_TwoZb;

		pOutParams->m_posPosCoef =  e1_Over_TwoZb*z2 - z2e2_Over_TwoZb + e2;
		pOutParams->m_posVelCoef = -e1_Over_TwoZb    + e2_Over_TwoZb;

		pOutParams->m_velPosCoef = (z1e1_Over_TwoZb - z2e2_Over_TwoZb + e2)*z2;
		pOutParams->m_velVelCoef = -z1e1_Over_TwoZb + z2e2_Over_TwoZb;
	}
	else if (dampingRatio < 1.0f - epsilon)
	{
		// under-damped
		float omegaZeta = angularFrequency * dampingRatio;
		float alpha     = angularFrequency * sqrtf(1.0f - dampingRatio*dampingRatio);

		float expTerm = expf( -omegaZeta * deltaTime );
		float cosTerm = cosf( alpha * deltaTime );
		float sinTerm = sinf( alpha * deltaTime );

		float invAlpha = 1.0f / alpha;

		float expSin = expTerm*sinTerm;
		float expCos = expTerm*cosTerm;
		float expOmegaZetaSin_Over_Alpha = expTerm*omegaZeta*sinTerm*invAlpha;

		pOutParams->m_posPosCoef = expCos + expOmegaZetaSin_Over_Alpha;
		pOutParams->m_posVelCoef = expSin*invAlpha;

		pOutParams->m_velPosCoef = -expSin*alpha - omegaZeta*expOmegaZetaSin_Over_Alpha;
		pOutParams->m_velVelCoef =  expCos - expOmegaZetaSin_Over_Alpha;
	}
	else
	{
		// critically damped
		float expTerm     = expf( -angularFrequency*deltaTime );
		float timeExp     = deltaTime*expTerm;
		float timeExpFreq = timeExp*angularFrequency;

		pOutParams->m_posPosCoef = timeExpFreq + expTerm;
		pOutParams->m_posVelCoef = timeExp;

		pOutParams->m_velPosCoef = -angularFrequency*timeExpFreq;
		pOutParams->m_velVelCoef = -timeExpFreq + expTerm;
	}
}

//******************************************************************************
// This function will update the supplied position and velocity values over
// according to the motion parameters.
//******************************************************************************
static inline void UpdateDampedSpringMotion(
	float*                           pPos,           // position value to update
	float*                           pVel,           // velocity value to update
	const float                      equilibriumPos, // position to approach
	const tDampedSpringMotionParams* params)         // motion parameters to use
{
	const float oldPos = *pPos - equilibriumPos; // update in equilibrium relative space
	const float oldVel = *pVel;

	(*pPos) = oldPos*params->m_posPosCoef + oldVel*params->m_posVelCoef + equilibriumPos;
	(*pVel) = oldPos*params->m_velPosCoef + oldVel*params->m_velVelCoef;
}
