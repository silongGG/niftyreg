/*
 *  _reg_localTransformation_jac.cpp
 *
 *
 *  Created by Marc Modat on 10/05/2011.
 *  Copyright (c) 2009, University College London. All rights reserved.
 *  Centre for Medical Image Computing (CMIC)
 *  See the LICENSE.txt file in the nifty_reg root folder
 *
 */

#include "_reg_localTransformation.h"

#define _USE_SQUARE_LOG_JAC

/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
void get_BSplineBasisValue(DTYPE basis, int index, DTYPE &value)
{
    switch(index){
    case 0: value = (DTYPE)((1.0-basis)*(1.0-basis)*(1.0-basis)/6.0);
        break;
    case 1: value = (DTYPE)((3.0*basis*basis*basis - 6.0*basis*basis + 4.0)/6.0);
        break;
    case 2: value = (DTYPE)((3.0*basis*basis - 3.0*basis*basis*basis + 3.0*basis + 1.0)/6.0);
        break;
    case 3: value = (DTYPE)(basis*basis*basis/6.0);
        break;
    default: value = (DTYPE)0;
        break;
    }
}
/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
void get_BSplineBasisValue(DTYPE basis, int index, DTYPE &value, DTYPE &first)
{
    get_BSplineBasisValue<DTYPE>(basis, index, value);
    switch(index){
    case 0: first = (DTYPE)((2.0*basis - basis*basis - 1.0)/2.0);
        break;
    case 1: first = (DTYPE)((3.0*basis*basis - 4.0*basis)/2.0);
        break;
    case 2: first = (DTYPE)((2.0*basis - 3.0*basis*basis + 1.0)/2.0);
        break;
    case 3: first = (DTYPE)(basis*basis/2.0);
        break;
    default: first = (DTYPE)0;
        break;
    }
}
/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
void get_BSplineBasisValue(DTYPE basis, int index, DTYPE &value, DTYPE &first, DTYPE &second)
{
    get_BSplineBasisValue<DTYPE>(basis, index, value, first);
    switch(index){
    case 0: second = (DTYPE)(1.0 - basis);
        break;
    case 1: second = (DTYPE)(3.0*basis -2.0);
        break;
    case 2: second = (DTYPE)(1.0 - 3.0*basis);
        break;
    case 3: second = (DTYPE)(basis);
        break;
    default: second = (DTYPE)0;
        break;
    }
}

/* *************************************************************** */
/* *************************************************************** */
template <class DTYPE>
void addJacobianGradientValues(mat33 jacobianMatrix,
                               double detJac,
                               DTYPE basisX,
                               DTYPE basisY,
                               DTYPE *jacobianConstraint)
{
    jacobianConstraint[0] += detJac * (jacobianMatrix.m[1][1]*basisX - jacobianMatrix.m[1][0]*basisY);
    jacobianConstraint[1] += detJac * (jacobianMatrix.m[0][0]*basisY - jacobianMatrix.m[0][1]*basisX);
}
/* *************************************************************** */
template <class DTYPE>
void addJacobianGradientValues(mat33 jacobianMatrix,
                               double detJac,
                               DTYPE basisX,
                               DTYPE basisY,
                               DTYPE basisZ,
                               DTYPE *jacobianConstraint)
{
    jacobianConstraint[0] += detJac * (
                basisX * (jacobianMatrix.m[1][1]*jacobianMatrix.m[2][2] - jacobianMatrix.m[1][2]*jacobianMatrix.m[2][1]) +
                basisY * (jacobianMatrix.m[1][2]*jacobianMatrix.m[2][0] - jacobianMatrix.m[1][0]*jacobianMatrix.m[2][2]) +
                basisZ * (jacobianMatrix.m[1][0]*jacobianMatrix.m[2][1] - jacobianMatrix.m[1][1]*jacobianMatrix.m[2][0]) );

    jacobianConstraint[1] += detJac * (
                basisX * (jacobianMatrix.m[0][2]*jacobianMatrix.m[2][1] - jacobianMatrix.m[0][1]*jacobianMatrix.m[2][2]) +
                basisY * (jacobianMatrix.m[0][0]*jacobianMatrix.m[2][2] - jacobianMatrix.m[0][2]*jacobianMatrix.m[2][0]) +
                basisZ * (jacobianMatrix.m[0][1]*jacobianMatrix.m[2][0] - jacobianMatrix.m[0][0]*jacobianMatrix.m[2][1]) );

    jacobianConstraint[2] += detJac * (
                basisX * (jacobianMatrix.m[0][1]*jacobianMatrix.m[1][2] - jacobianMatrix.m[0][2]*jacobianMatrix.m[1][1]) +
                basisY * (jacobianMatrix.m[0][2]*jacobianMatrix.m[1][0] - jacobianMatrix.m[0][0]*jacobianMatrix.m[1][2]) +
                basisZ * (jacobianMatrix.m[0][0]*jacobianMatrix.m[1][1] - jacobianMatrix.m[0][1]*jacobianMatrix.m[1][0]) );
}
/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
double reg_spline_jacobianValue2D(nifti_image *splineControlPoint,
                                  nifti_image *referenceImage)
{
    DTYPE *controlPointPtrX = static_cast<DTYPE *>
            (splineControlPoint->data);
    DTYPE *controlPointPtrY = static_cast<DTYPE *>
            (&controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny]);

    DTYPE yBasis[4],yFirst[4],temp[4],first[4];
    DTYPE basisX[16], basisY[16], basis;

    DTYPE xControlPointCoordinates[16];
    DTYPE yControlPointCoordinates[16];

    DTYPE gridVoxelSpacing[2];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    int x, y, a, b, xPre, yPre, coord, oldXpre, oldYpre;
    DTYPE Tx_x, Tx_y, Ty_x, Ty_y;
    double detJac, logJac, constraintValue=0;

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(referenceImage, gridVoxelSpacing, splineControlPoint, \
    controlPointPtrX, controlPointPtrY, reorient, realSpacing) \
    private(x, y, a, b, xPre, yPre, oldXpre, oldYpre, basis, \
    temp, first, yBasis, yFirst, coord, jacobianMatrix, detJac, logJac, \
    xControlPointCoordinates, yControlPointCoordinates, basisX, basisY, \
    Tx_x, Tx_y, Ty_x, Ty_y) \
    reduction(+:constraintValue)
#endif
    for(y=0; y<referenceImage->ny; y++){
        oldXpre=oldYpre=9999999;

        yPre=(int)((DTYPE)y/gridVoxelSpacing[1]);
        basis=(DTYPE)y/gridVoxelSpacing[1]-(DTYPE)yPre;
        if(basis<0.0) basis=0.0; //rounding error
        get_BSplineBasisValues<DTYPE>(basis, yBasis, yFirst);

        for(x=0; x<referenceImage->nx; x++){

            xPre=(int)((DTYPE)x/gridVoxelSpacing[0]);
            basis=(DTYPE)x/gridVoxelSpacing[0]-(DTYPE)xPre;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, temp, first);

            coord=0;
            for(b=0; b<4; b++){
                for(a=0; a<4; a++){
                    basisX[coord]=yBasis[b]*first[a];   // y * x'
                    basisY[coord]=yFirst[b]*temp[a];    // y'* x
                    coord++;
                }
            }

            if(xPre!=oldXpre || yPre!=oldYpre){
                get_GridValues<DTYPE>(xPre,
                                      yPre,
                                      splineControlPoint,
                                      controlPointPtrX,
                                      controlPointPtrY,
                                      xControlPointCoordinates,
                                      yControlPointCoordinates,
                                      false, // no approx
                                      false // not disp
                                      );
                oldXpre=xPre;oldYpre=yPre;
            }

            Tx_x=0.0; Ty_x=0.0; Tx_y=0.0; Ty_y=0.0;

            for(a=0; a<16; a++){
                Tx_x += basisX[a]*xControlPointCoordinates[a];
                Tx_y += basisY[a]*xControlPointCoordinates[a];

                Ty_x += basisX[a]*yControlPointCoordinates[a];
                Ty_y += basisY[a]*yControlPointCoordinates[a];
            }

            memset(&jacobianMatrix, 0, sizeof(mat33));
            jacobianMatrix.m[2][2]=1.0f;
            jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
            jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
            jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
            jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);

            jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
            detJac = nifti_mat33_determ(jacobianMatrix);
            if(detJac>0.0){
                logJac = log(detJac);
#ifdef _USE_SQUARE_LOG_JAC
                constraintValue += logJac*logJac;
#else
                constraintValue +=  fabs(logJac);
#endif
            }
            else
#ifdef _OPENMP
                constraintValue=std::numeric_limits<double>::quiet_NaN();
#else // _OPENMP
                return std::numeric_limits<double>::quiet_NaN();
#endif // _OPENMP
        }
    }
    return constraintValue/(double)(referenceImage->nx*referenceImage->ny*referenceImage->nz);
}
/* *************************************************************** */
template<class DTYPE>
double reg_spline_jacobianValue3D(nifti_image *splineControlPoint,
                                  nifti_image *referenceImage)
{
#if _USE_SSE
    if(sizeof(DTYPE)!=4){
        fprintf(stderr, "[NiftyReg ERROR] computeJacobianMatrices_3D\n");
        fprintf(stderr, "[NiftyReg ERROR] The SSE implementation assume single precision... Exit\n");
        exit(1);
    }
    union{__m128 m;float f[4];} val;
    __m128 _xBasis, _xFirst, _yBasis, _yFirst;
    __m128 tempX_x, tempX_y, tempX_z, tempY_x, tempY_y, tempY_z, tempZ_x, tempZ_y, tempZ_z;
#ifdef _WINDOWS
    union{__m128 m[4];__declspec(align(16)) DTYPE f[16];} tempX;
    union{__m128 m[4];__declspec(align(16)) DTYPE f[16];} tempY;
    union{__m128 m[4];__declspec(align(16)) DTYPE f[16];} tempZ;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisX;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisY;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisZ;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} xControlPointCoordinates;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} yControlPointCoordinates;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} zControlPointCoordinates;
#else // _WINDOWS
    union{__m128 m[4];DTYPE f[16] __attribute__((aligned(16)));} tempX;
    union{__m128 m[4];DTYPE f[16] __attribute__((aligned(16)));} tempY;
    union{__m128 m[4];DTYPE f[16] __attribute__((aligned(16)));} tempZ;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisX;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisY;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisZ;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} xControlPointCoordinates;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} yControlPointCoordinates;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} zControlPointCoordinates;

#endif // _WINDOWS
#else
    int coord, b, c, bc;
    DTYPE tempX[16], tempY[16], tempZ[16];
    DTYPE basisX[64], basisY[64], basisZ[64];
    DTYPE xControlPointCoordinates[64];
    DTYPE yControlPointCoordinates[64];
    DTYPE zControlPointCoordinates[64];
#endif

    DTYPE *controlPointPtrX = static_cast<DTYPE *>
            (splineControlPoint->data);
    DTYPE *controlPointPtrY = static_cast<DTYPE *>
            (&controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);
    DTYPE *controlPointPtrZ = static_cast<DTYPE *>
            (&controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);

    DTYPE zBasis[4],zFirst[4],temp[4], first[4], basis;

    DTYPE gridVoxelSpacing[3];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;
    gridVoxelSpacing[2] = splineControlPoint->dz / referenceImage->dz;
    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    int x, y, z, xPre, yPre, zPre, a, oldXpre, oldYpre, oldZpre;
    DTYPE Tx_x, Tx_y, Tx_z;
    DTYPE Ty_x, Ty_y, Ty_z;
    DTYPE Tz_x, Tz_y, Tz_z, detJac;
    double constraintValue=0, logJac;
#ifdef _OPENMP
#ifdef _USE_SSE
#pragma omp parallel for default(none) \
    shared(referenceImage, gridVoxelSpacing, splineControlPoint, realSpacing, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, reorient) \
    private(x, y, z, xPre, yPre, zPre, a, basis, val, \
    _xBasis, _xFirst, _yBasis, _yFirst, \
    tempX, tempY, tempZ, basisX, basisY, basisZ, \
    oldXpre, oldYpre, oldZpre, zBasis, zFirst, temp, first, detJac, logJac, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    Tx_x, Tx_y, Tx_z, Ty_x, Ty_y, Ty_z, Tz_x, Tz_y, Tz_z, jacobianMatrix, \
    tempX_x, tempX_y, tempX_z, tempY_x, tempY_y, tempY_z, tempZ_x, tempZ_y, tempZ_z) \
    reduction(+:constraintValue)
#else // _USE_SEE
#pragma omp parallel for default(none) \
    shared(referenceImage, gridVoxelSpacing, splineControlPoint, realSpacing, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, reorient) \
    private(x, y, z, xPre, yPre, zPre, a, b, c, bc, basis, detJac, logJac,\
    basisX, basisY, basisZ, coord, tempX, tempY, tempZ, temp, first, \
    zBasis, zFirst, oldXpre, oldYpre, oldZpre, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    Tx_x, Tx_y, Tx_z, Ty_x, Ty_y, Ty_z, Tz_x, Tz_y, Tz_z, jacobianMatrix) \
    reduction(+:constraintValue)
#endif // _USE_SEE
#endif // _USE_OPENMP
    for(z=0; z<referenceImage->nz; z++){
        oldXpre=999999, oldYpre=999999, oldZpre=999999;

        zPre=(int)((DTYPE)z/gridVoxelSpacing[2]);
        basis=(DTYPE)z/gridVoxelSpacing[2]-(DTYPE)zPre;
        if(basis<0.0) basis=0.0; //rounding error
        get_BSplineBasisValues<DTYPE>(basis, zBasis, zFirst);

        for(y=0; y<referenceImage->ny; y++){

            yPre=(int)((DTYPE)y/gridVoxelSpacing[1]);
            basis=(DTYPE)y/gridVoxelSpacing[1]-(DTYPE)yPre;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, temp, first);

#if _USE_SSE
            val.f[0]=temp[0];
            val.f[1]=temp[1];
            val.f[2]=temp[2];
            val.f[3]=temp[3];
            _yBasis=val.m;
            val.f[0]=first[0];
            val.f[1]=first[1];
            val.f[2]=first[2];
            val.f[3]=first[3];
            _yFirst=val.m;
            for(a=0;a<4;++a){
                val.m=_mm_set_ps1(zBasis[a]);
                tempX.m[a]=_mm_mul_ps(_yBasis,val.m);
                tempY.m[a]=_mm_mul_ps(_yFirst,val.m);
                val.m=_mm_set_ps1(zFirst[a]);
                tempZ.m[a]=_mm_mul_ps(_yBasis,val.m);
            }
#else
            coord=0;
            for(c=0; c<4; c++){
                for(b=0; b<4; b++){
                    tempX[coord]=zBasis[c]*temp[b]; // z * y
                    tempY[coord]=zBasis[c]*first[b];// z * y'
                    tempZ[coord]=zFirst[c]*temp[b]; // z'* y
                    coord++;
                }
            }
#endif
            for(x=0; x<referenceImage->nx; x++){

                xPre=(int)((DTYPE)x/gridVoxelSpacing[0]);
                basis=(DTYPE)x/gridVoxelSpacing[0]-(DTYPE)xPre;
                if(basis<0.0) basis=0.0; //rounding error
                get_BSplineBasisValues<DTYPE>(basis, temp, first);

#if _USE_SSE
                val.f[0]=temp[0];
                val.f[1]=temp[1];
                val.f[2]=temp[2];
                val.f[3]=temp[3];
                _xBasis=val.m;
                val.f[0]=first[0];
                val.f[1]=first[1];
                val.f[2]=first[2];
                val.f[3]=first[3];
                _xFirst=val.m;
                for(a=0;a<16;++a){
                    val.m=_mm_set_ps1(tempX.f[a]);
                    basisX.m[a]=_mm_mul_ps(_xFirst,val.m);
                    val.m=_mm_set_ps1(tempY.f[a]);
                    basisY.m[a]=_mm_mul_ps(_xBasis,val.m);
                    val.m=_mm_set_ps1(tempZ.f[a]);
                    basisZ.m[a]=_mm_mul_ps(_xBasis,val.m);
                }
#else
                coord=0;
                for(bc=0; bc<16; bc++){
                    for(a=0; a<4; a++){
                        basisX[coord]=tempX[bc]*first[a];   // z * y * x'
                        basisY[coord]=tempY[bc]*temp[a];    // z * y'* x
                        basisZ[coord]=tempZ[bc]*temp[a];    // z'* y * x
                        coord++;
                    }
                }
#endif

                if(oldXpre!=xPre || oldYpre!=yPre || oldZpre!=zPre){
#ifdef _USE_SSE
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates.f,
                                          yControlPointCoordinates.f,
                                          zControlPointCoordinates.f,
                                          false, // no approx
                                          false // not disp
                                          );
#else // _USE_SSE
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates,
                                          yControlPointCoordinates,
                                          zControlPointCoordinates,
                                          false, // no approx
                                          false // not disp
                                          );
#endif // _USE_SSE
                    oldXpre=xPre;oldYpre=yPre;oldZpre=zPre;
                }

                Tx_x=0.0;
                Ty_x=0.0;
                Tz_x=0.0;
                Tx_y=0.0;
                Ty_y=0.0;
                Tz_y=0.0;
                Tx_z=0.0;
                Ty_z=0.0;
                Tz_z=0.0;

#if _USE_SSE
                tempX_x =  _mm_set_ps1(0.0);
                tempX_y =  _mm_set_ps1(0.0);
                tempX_z =  _mm_set_ps1(0.0);
                tempY_x =  _mm_set_ps1(0.0);
                tempY_y =  _mm_set_ps1(0.0);
                tempY_z =  _mm_set_ps1(0.0);
                tempZ_x =  _mm_set_ps1(0.0);
                tempZ_y =  _mm_set_ps1(0.0);
                tempZ_z =  _mm_set_ps1(0.0);
                //addition and multiplication of the 16 basis value and CP position for each axis
                for(a=0; a<16; a++){
                    tempX_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], xControlPointCoordinates.m[a]), tempX_x );
                    tempX_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], xControlPointCoordinates.m[a]), tempX_y );
                    tempX_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], xControlPointCoordinates.m[a]), tempX_z );

                    tempY_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], yControlPointCoordinates.m[a]), tempY_x );
                    tempY_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], yControlPointCoordinates.m[a]), tempY_y );
                    tempY_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], yControlPointCoordinates.m[a]), tempY_z );

                    tempZ_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], zControlPointCoordinates.m[a]), tempZ_x );
                    tempZ_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], zControlPointCoordinates.m[a]), tempZ_y );
                    tempZ_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], zControlPointCoordinates.m[a]), tempZ_z );
                }

                //the values stored in SSE variables are transfered to normal float
                val.m = tempX_x;
                Tx_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempX_y;
                Tx_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempX_z;
                Tx_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];

                val.m = tempY_x;
                Ty_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempY_y;
                Ty_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempY_z;
                Ty_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];

                val.m = tempZ_x;
                Tz_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempZ_y;
                Tz_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempZ_z;
                Tz_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];
#else
                for(a=0; a<64; a++){
                    Tx_x += basisX[a]*xControlPointCoordinates[a];
                    Tx_y += basisY[a]*xControlPointCoordinates[a];
                    Tx_z += basisZ[a]*xControlPointCoordinates[a];

                    Ty_x += basisX[a]*yControlPointCoordinates[a];
                    Ty_y += basisY[a]*yControlPointCoordinates[a];
                    Ty_z += basisZ[a]*yControlPointCoordinates[a];

                    Tz_x += basisX[a]*zControlPointCoordinates[a];
                    Tz_y += basisY[a]*zControlPointCoordinates[a];
                    Tz_z += basisZ[a]*zControlPointCoordinates[a];
                }
#endif

                jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
                jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
                jacobianMatrix.m[0][2]= (float)(Tx_z / realSpacing[2]);
                jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
                jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
                jacobianMatrix.m[1][2]= (float)(Ty_z / realSpacing[2]);
                jacobianMatrix.m[2][0]= (float)(Tz_x / realSpacing[0]);
                jacobianMatrix.m[2][1]= (float)(Tz_y / realSpacing[1]);
                jacobianMatrix.m[2][2]= (float)(Tz_z / realSpacing[2]);

                jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
                detJac = nifti_mat33_determ(jacobianMatrix);

                if(detJac>0.0){
                    logJac = log(detJac);
#ifdef _USE_SQUARE_LOG_JAC
                    constraintValue += logJac*logJac;
#else
                    constraintValue +=  fabs(log(detJac));
#endif
                }
                else
#ifdef _OPENMP
                    constraintValue=std::numeric_limits<double>::quiet_NaN();
#else // _OPENMP
                    return std::numeric_limits<double>::quiet_NaN();
#endif // _OPENMP
            }
        }
    }

    return constraintValue/(double)(referenceImage->nx*referenceImage->ny*referenceImage->nz);
}
/* *************************************************************** */
template<class DTYPE>
double reg_spline_jacobianApproxValue2D(nifti_image *splineControlPoint)
{
    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = static_cast<DTYPE *>(&controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny]);

    // As the contraint is only computed at the voxel position, the basis value of the spline are always the same
    DTYPE basisX[9], basisY[9], xControlPointCoordinates[9], yControlPointCoordinates[9];
    DTYPE normal[3]={1.0/6.0, 2.0/3.0, 1.0/6.0};
    DTYPE first[3]={-0.5, 0.0, 0.5};
    unsigned int coord=0;
    for(int b=0; b<3; b++){
        for(int a=0; a<3; a++){
            basisX[coord]=first[a]*normal[b];
            basisY[coord]=normal[a]*first[b];
            coord++;
        }
    }

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    double constraintValue=0, logJac, detJac;
    int x, y, a;
    DTYPE Tx_x, Ty_y, Tx_y, Ty_x;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, controlPointPtrX, controlPointPtrY, \
    reorient, basisX, basisY, realSpacing) \
    private(x, y, a, jacobianMatrix, detJac, logJac, Tx_x, Ty_y, Tx_y, Ty_x, \
    xControlPointCoordinates, yControlPointCoordinates) \
    reduction(+:constraintValue)
#endif
    for(y=1;y<splineControlPoint->ny-1;y++){
        for(x=1;x<splineControlPoint->nx-1;x++){

            get_GridValues<DTYPE>(x-1,
                                  y-1,
                                  splineControlPoint,
                                  controlPointPtrX,
                                  controlPointPtrY,
                                  xControlPointCoordinates,
                                  yControlPointCoordinates,
                                  true, // approx
                                  false // not disp
                                  );

            Tx_x=0.0;
            Ty_x=0.0;
            Tx_y=0.0;
            Ty_y=0.0;

            for(a=0; a<9; a++){
                Tx_x += basisX[a]*xControlPointCoordinates[a];
                Tx_y += basisY[a]*xControlPointCoordinates[a];

                Ty_x += basisX[a]*yControlPointCoordinates[a];
                Ty_y += basisY[a]*yControlPointCoordinates[a];
            }

            memset(&jacobianMatrix, 0, sizeof(mat33));
            jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
            jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
            jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
            jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
            jacobianMatrix.m[2][2]=1.0f;

            jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
            detJac = jacobianMatrix.m[0][0]*jacobianMatrix.m[1][1]-
                    jacobianMatrix.m[0][1]*jacobianMatrix.m[1][0];

            if(detJac>0.0){
                logJac = log(detJac);
#ifdef _USE_SQUARE_LOG_JAC
                constraintValue += logJac*logJac;
#else
                constraintValue +=  fabs(detJac);
#endif
            }
            else
#ifdef _OPENMP
                constraintValue=std::numeric_limits<double>::quiet_NaN();
#else // _OPENMP
                return std::numeric_limits<double>::quiet_NaN();
#endif // _OPENMP
        }
    }
    return constraintValue/(double)((splineControlPoint->nx-2)*(splineControlPoint->ny-2));
}
/* *************************************************************** */
template<class DTYPE>
double reg_spline_jacobianApproxValue3D(nifti_image *splineControlPoint)
{
    // As the contraint is only computed at the voxel position, the basis value of the spline are always the same
    float basisX[27], basisY[27], basisZ[27];
    DTYPE xControlPointCoordinates[27], yControlPointCoordinates[27], zControlPointCoordinates[27];
    DTYPE normal[3]={1.0/6.0, 2.0/3.0, 1.0/6.0};
    DTYPE first[3]={-0.5, 0, 0.5};
    // There are six different values taken into account
    DTYPE tempX[9], tempY[9], tempZ[9];
    int coord=0;
    for(int c=0; c<3; c++){
        for(int b=0; b<3; b++){
            tempX[coord]=normal[c]*normal[b];  // z * y
            tempY[coord]=normal[c]*first[b];  // z * y"
            tempZ[coord]=first[c]*normal[b];  // z"* y
            coord++;
        }
    }
    coord=0;
    for(int bc=0; bc<9; bc++){
        for(int a=0; a<3; a++){
            basisX[coord]=tempX[bc]*first[a];    // z * y * x"
            basisY[coord]=tempY[bc]*normal[a];    // z * y"* x
            basisZ[coord]=tempZ[bc]*normal[a];    // z"* y * x
            coord++;
        }
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = static_cast<DTYPE *>
            (&controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);
    DTYPE *controlPointPtrZ = static_cast<DTYPE *>
            (&controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    DTYPE Tx_x, Ty_x, Tz_x;
    DTYPE Tx_y, Ty_y, Tz_y;
    DTYPE Tx_z, Ty_z, Tz_z;
    DTYPE detJac;

    int x,y,z, a;
    double constraintValue=0.0, logJac;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, controlPointPtrX, controlPointPtrY, controlPointPtrZ, \
    basisX, basisY, basisZ, reorient, realSpacing) \
    private(x, y, z, a, Tx_x, Ty_x, Tz_x, Tx_y, Ty_y, Tz_y, Tx_z, Ty_z, Tz_z, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    jacobianMatrix, detJac, logJac) \
    reduction(+:constraintValue)
#endif
    for(z=1;z<splineControlPoint->nz-1;z++){
        for(y=1;y<splineControlPoint->ny-1;y++){
            for(x=1;x<splineControlPoint->nx-1;x++){

                get_GridValues<DTYPE>(x-1,
                                      y-1,
                                      z-1,
                                      splineControlPoint,
                                      controlPointPtrX,
                                      controlPointPtrY,
                                      controlPointPtrZ,
                                      xControlPointCoordinates,
                                      yControlPointCoordinates,
                                      zControlPointCoordinates,
                                      true, // approx
                                      false // not disp
                                      );

                Tx_x=0.0; Ty_x=0.0; Tz_x=0.0;
                Tx_y=0.0; Ty_y=0.0; Tz_y=0.0;
                Tx_z=0.0; Ty_z=0.0; Tz_z=0.0;

                for(a=0; a<27; a++){
                    Tx_x += basisX[a]*xControlPointCoordinates[a];
                    Tx_y += basisY[a]*xControlPointCoordinates[a];
                    Tx_z += basisZ[a]*xControlPointCoordinates[a];

                    Ty_x += basisX[a]*yControlPointCoordinates[a];
                    Ty_y += basisY[a]*yControlPointCoordinates[a];
                    Ty_z += basisZ[a]*yControlPointCoordinates[a];

                    Tz_x += basisX[a]*zControlPointCoordinates[a];
                    Tz_y += basisY[a]*zControlPointCoordinates[a];
                    Tz_z += basisZ[a]*zControlPointCoordinates[a];
                }

                jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
                jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
                jacobianMatrix.m[0][2]= (float)(Tx_z / realSpacing[2]);
                jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
                jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
                jacobianMatrix.m[1][2]= (float)(Ty_z / realSpacing[2]);
                jacobianMatrix.m[2][0]= (float)(Tz_x / realSpacing[0]);
                jacobianMatrix.m[2][1]= (float)(Tz_y / realSpacing[1]);
                jacobianMatrix.m[2][2]= (float)(Tz_z / realSpacing[2]);

                jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
                detJac = nifti_mat33_determ(jacobianMatrix);

                if(detJac>0.0){
                    logJac = log(detJac);
#ifdef _USE_SQUARE_LOG_JAC
                    constraintValue += logJac*logJac;
#else
                    constraintValue +=  fabs(log(detJac));
#endif
                }
                else
#ifdef _OPENMP
                    constraintValue=std::numeric_limits<double>::quiet_NaN();
#else // _OPENMP
                    return std::numeric_limits<double>::quiet_NaN();
#endif // _OPENMP
            }
        }
    }

    return constraintValue/(double)((splineControlPoint->nx-2)*(splineControlPoint->ny-2)*(splineControlPoint->nz-2));
}
/* *************************************************************** */
extern "C++"
double reg_spline_getJacobianPenaltyTerm(nifti_image *splineControlPoint,
                                         nifti_image *referenceImage,
                                         bool approx
                                         )
{
    if(splineControlPoint->nz==1){
        switch(splineControlPoint->datatype){
        case NIFTI_TYPE_FLOAT32:
            if(approx)
                return reg_spline_jacobianApproxValue2D<float>(splineControlPoint);
            else return reg_spline_jacobianValue2D<float>(splineControlPoint, referenceImage);
            break;
        case NIFTI_TYPE_FLOAT64:
            if(approx)
                return reg_spline_jacobianApproxValue2D<double>(splineControlPoint);
            else return reg_spline_jacobianValue2D<double>(splineControlPoint, referenceImage);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the jacobian value\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian value is not computed\n");
            exit(1);
        }
    }
    else{
        switch(splineControlPoint->datatype){
        case NIFTI_TYPE_FLOAT32:
            if(approx)
                return reg_spline_jacobianApproxValue3D<float>(splineControlPoint);
            else return reg_spline_jacobianValue3D<float>(splineControlPoint, referenceImage);
            break;
        case NIFTI_TYPE_FLOAT64:
            if(approx)
                return reg_spline_jacobianApproxValue3D<double>(splineControlPoint);
            else return reg_spline_jacobianValue3D<double>(splineControlPoint, referenceImage);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the jacobian value\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian value is not computed\n");
            exit(1);
        }

    }
}
/* *************************************************************** */
/* *************************************************************** */
template <class DTYPE>
void reg_spline_computeJacobianMatrices_2D(nifti_image *referenceImage,
                                           nifti_image *splineControlPoint,
                                           mat33 *jacobianMatrices,
                                           DTYPE *jacobianDeterminant)
{

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny];

    DTYPE yBasis[4],yFirst[4],xBasis[4],xFirst[4];
    DTYPE basisX[16], basisY[16], basis;

    DTYPE xControlPointCoordinates[16];
    DTYPE yControlPointCoordinates[16];

    DTYPE gridVoxelSpacing[2];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    int index, coord, x, y, a, b, xPre, yPre, oldXpre, oldYpre;
    DTYPE Tx_x, Tx_y, Ty_x, Ty_y;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(referenceImage, gridVoxelSpacing, splineControlPoint, reorient, realSpacing, \
    controlPointPtrX, controlPointPtrY, jacobianDeterminant, jacobianMatrices) \
    private(index, coord, x, y, a, b, xPre, yPre, basis, xBasis, xFirst, yBasis, yFirst, \
    oldXpre, oldYpre, basisX, basisY, xControlPointCoordinates, yControlPointCoordinates, \
    Tx_x, Tx_y, Ty_x, Ty_y, jacobianMatrix)
#endif
    for(y=0; y<referenceImage->ny; y++){
        index=y*referenceImage->nx;
        oldXpre=oldYpre=9999999;

        yPre=(int)((DTYPE)y/gridVoxelSpacing[1]);
        basis=(DTYPE)y/gridVoxelSpacing[1]-(DTYPE)yPre;
        if(basis<0.0) basis=0.0; //rounding error
        get_BSplineBasisValues<DTYPE>(basis, yBasis, yFirst);

        for(x=0; x<referenceImage->nx; x++){

            xPre=(int)((DTYPE)x/gridVoxelSpacing[0]);
            basis=(DTYPE)x/gridVoxelSpacing[0]-(DTYPE)xPre;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, xBasis, xFirst);

            coord=0;
            for(b=0; b<4; b++){
                for(a=0; a<4; a++){
                    basisX[coord]=yBasis[b]*xFirst[a];   // y * x'
                    basisY[coord]=yFirst[b]*xBasis[a];    // y'* x
                    coord++;
                }
            }

            if(xPre!=oldXpre || yPre!=oldYpre){
                get_GridValues<DTYPE>(xPre,
                                      yPre,
                                      splineControlPoint,
                                      controlPointPtrX,
                                      controlPointPtrY,
                                      xControlPointCoordinates,
                                      yControlPointCoordinates,
                                      false, // no approx
                                      false // not disp
                                      );
                oldXpre=xPre;oldYpre=yPre;
            }

            Tx_x=0.0;
            Ty_x=0.0;
            Tx_y=0.0;
            Ty_y=0.0;

            for(a=0; a<16; a++){
                Tx_x += basisX[a]*xControlPointCoordinates[a];
                Tx_y += basisY[a]*xControlPointCoordinates[a];

                Ty_x += basisX[a]*yControlPointCoordinates[a];
                Ty_y += basisY[a]*yControlPointCoordinates[a];
            }

            memset(&jacobianMatrix, 0, sizeof(mat33));
            jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
            jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
            jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
            jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
            jacobianMatrix.m[2][2]=1.0f;

            jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);

            if(jacobianDeterminant!=NULL)
                jacobianDeterminant[index] = jacobianMatrix.m[0][0]*jacobianMatrix.m[1][1] -
                        jacobianMatrix.m[0][1]*jacobianMatrix.m[1][0];
            if(jacobianMatrices!=NULL)
                jacobianMatrices[index] = jacobianMatrix;
            index++;
        }
    }
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_computeJacobianMatrices_3D(nifti_image *referenceImage,
                                           nifti_image *splineControlPoint,
                                           mat33 *jacobianMatrices,
                                           DTYPE *jacobianDeterminant)
{
#if _USE_SSE
    if(sizeof(DTYPE)!=4){
        fprintf(stderr, "[NiftyReg ERROR] computeJacobianMatrices_3D\n");
        fprintf(stderr, "[NiftyReg ERROR] The SSE implementation assume single precision... Exit\n");
        exit(1);
    }
    union{
        __m128 m;
        float f[4];
    } val;
    __m128 _xBasis, _xFirst, _yBasis, _yFirst;
    __m128 tempX_x, tempX_y, tempX_z, tempY_x, tempY_y, tempY_z, tempZ_x, tempZ_y, tempZ_z;
#ifdef _WINDOWS
    union{__m128 m[4];__declspec(align(16)) DTYPE f[16];} tempX;
    union{__m128 m[4];__declspec(align(16)) DTYPE f[16];} tempY;
    union{__m128 m[4];__declspec(align(16)) DTYPE f[16];} tempZ;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisX;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisY;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisZ;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} xControlPointCoordinates;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} yControlPointCoordinates;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} zControlPointCoordinates;
#else // _WINDOWS
    union{__m128 m[4];DTYPE f[16] __attribute__((aligned(16)));} tempX;
    union{__m128 m[4];DTYPE f[16] __attribute__((aligned(16)));} tempY;
    union{__m128 m[4];DTYPE f[16] __attribute__((aligned(16)));} tempZ;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisX;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisY;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisZ;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} xControlPointCoordinates;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} yControlPointCoordinates;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} zControlPointCoordinates;
#endif // _WINDOWS
#else
    int coord, b, c, bc;
    DTYPE tempX[16], tempY[16], tempZ[16], basisX[64], basisY[64], basisZ[64];
    DTYPE xControlPointCoordinates[64];
    DTYPE yControlPointCoordinates[64];
    DTYPE zControlPointCoordinates[64];
#endif
    DTYPE yBasis[4], yFirst[4], xBasis[4], xFirst[4] ,zBasis[4] ,zFirst[4], basis;

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];
    DTYPE *controlPointPtrZ = &controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];

    DTYPE gridVoxelSpacing[3];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;
    gridVoxelSpacing[2] = splineControlPoint->dz / referenceImage->dz;

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    int index, x, y, z, xPre, yPre, zPre, a, oldXpre, oldYpre, oldZpre;
    DTYPE Tx_x, Tx_y, Tx_z;
    DTYPE Ty_x, Ty_y, Ty_z;
    DTYPE Tz_x, Tz_y, Tz_z;
#ifdef _OPENMP
#ifdef _USE_SSE
#pragma omp parallel for default(none) \
    shared(referenceImage, gridVoxelSpacing, splineControlPoint, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, reorient, \
    jacobianDeterminant, jacobianMatrices, realSpacing) \
    private(x, y, z, xPre, yPre, zPre, a, index, basis, val, \
    _xBasis, _xFirst, _yBasis, _yFirst, \
    tempX, tempY, tempZ, basisX, basisY, basisZ, \
    xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, oldXpre, oldYpre, oldZpre, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    Tx_x, Tx_y, Tx_z, Ty_x, Ty_y, Ty_z, Tz_x, Tz_y, Tz_z, jacobianMatrix, \
    tempX_x, tempX_y, tempX_z, tempY_x, tempY_y, tempY_z, tempZ_x, tempZ_y, tempZ_z)
#else // _USE_SEE
#pragma omp parallel for default(none) \
    shared(referenceImage, gridVoxelSpacing, splineControlPoint, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, reorient, \
    jacobianDeterminant, jacobianMatrices, realSpacing) \
    private(x, y, z, xPre, yPre, zPre, a, b, c, bc, index, basis, \
    basisX, basisY, basisZ, coord, tempX, tempY, tempZ, \
    xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, oldXpre, oldYpre, oldZpre, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    Tx_x, Tx_y, Tx_z, Ty_x, Ty_y, Ty_z, Tz_x, Tz_y, Tz_z, jacobianMatrix)
#endif // _USE_SEE
#endif // _USE_OPENMP
    for(z=0; z<referenceImage->nz; z++){
        oldXpre=999999, oldYpre=999999, oldZpre=999999;
        index=z*referenceImage->nx*referenceImage->ny;

        zPre=(int)((DTYPE)z/gridVoxelSpacing[2]);
        basis=(DTYPE)z/gridVoxelSpacing[2]-(DTYPE)zPre;
        if(basis<0.0) basis=0.0; //rounding error
        get_BSplineBasisValues<DTYPE>(basis, zBasis, zFirst);

        for(y=0; y<referenceImage->ny; y++){

            yPre=(int)((DTYPE)y/gridVoxelSpacing[1]);
            basis=(DTYPE)y/gridVoxelSpacing[1]-(DTYPE)yPre;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, yBasis, yFirst);
#ifdef _USE_SSE
            val.f[0]=yBasis[0];
            val.f[1]=yBasis[1];
            val.f[2]=yBasis[2];
            val.f[3]=yBasis[3];
            _yBasis=val.m;
            val.f[0]=yFirst[0];
            val.f[1]=yFirst[1];
            val.f[2]=yFirst[2];
            val.f[3]=yFirst[3];
            _yFirst=val.m;
            for(a=0;a<4;++a){
                val.m=_mm_set_ps1(zBasis[a]);
                tempX.m[a]=_mm_mul_ps(_yBasis,val.m);
                tempY.m[a]=_mm_mul_ps(_yFirst,val.m);
                val.m=_mm_set_ps1(zFirst[a]);
                tempZ.m[a]=_mm_mul_ps(_yBasis,val.m);
            }
#else
            coord=0;
            for(c=0; c<4; c++){
                for(b=0; b<4; b++){
                    tempX[coord]=zBasis[c]*yBasis[b]; // z * y
                    tempY[coord]=zBasis[c]*yFirst[b]; // z * y'
                    tempZ[coord]=zFirst[c]*yBasis[b]; // z'* y
                    coord++;
                }
            }
#endif

            for(x=0; x<referenceImage->nx; x++){

                xPre=(int)((DTYPE)x/gridVoxelSpacing[0]);
                basis=(DTYPE)x/gridVoxelSpacing[0]-(DTYPE)xPre;
                if(basis<0.0) basis=0.0; //rounding error
                get_BSplineBasisValues<DTYPE>(basis, xBasis, xFirst);

#ifdef _USE_SSE
                val.f[0]=xBasis[0];
                val.f[1]=xBasis[1];
                val.f[2]=xBasis[2];
                val.f[3]=xBasis[3];
                _xBasis=val.m;
                val.f[0]=xFirst[0];
                val.f[1]=xFirst[1];
                val.f[2]=xFirst[2];
                val.f[3]=xFirst[3];
                _xFirst=val.m;
                for(a=0;a<16;++a){
                    val.m=_mm_set_ps1(tempX.f[a]);
                    basisX.m[a]=_mm_mul_ps(_xFirst,val.m);
                    val.m=_mm_set_ps1(tempY.f[a]);
                    basisY.m[a]=_mm_mul_ps(_xBasis,val.m);
                    val.m=_mm_set_ps1(tempZ.f[a]);
                    basisZ.m[a]=_mm_mul_ps(_xBasis,val.m);
                }
#else
                coord=0;
                for(bc=0; bc<16; bc++){
                    for(a=0; a<4; a++){
                        basisX[coord]=tempX[bc]*xFirst[a];   // z * y * x'
                        basisY[coord]=tempY[bc]*xBasis[a];    // z * y'* x
                        basisZ[coord]=tempZ[bc]*xBasis[a];    // z'* y * x
                        coord++;
                    }
                }
#endif

                if(xPre!=oldXpre || yPre!=oldYpre || zPre!=oldZpre){
#ifdef _USE_SSE
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates.f,
                                          yControlPointCoordinates.f,
                                          zControlPointCoordinates.f,
                                          false, // no approx
                                          false // not disp
                                          );
#else // _USE_SSE
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates,
                                          yControlPointCoordinates,
                                          zControlPointCoordinates,
                                          false, // no approx
                                          false // not disp
                                          );
#endif // _USE_SSE
                    oldXpre=xPre; oldYpre=yPre; oldZpre=zPre;
                }

                Tx_x=0.0;
                Ty_x=0.0;
                Tz_x=0.0;
                Tx_y=0.0;
                Ty_y=0.0;
                Tz_y=0.0;
                Tx_z=0.0;
                Ty_z=0.0;
                Tz_z=0.0;

#ifdef _USE_SSE
                tempX_x =  _mm_set_ps1(0.0);
                tempX_y =  _mm_set_ps1(0.0);
                tempX_z =  _mm_set_ps1(0.0);
                tempY_x =  _mm_set_ps1(0.0);
                tempY_y =  _mm_set_ps1(0.0);
                tempY_z =  _mm_set_ps1(0.0);
                tempZ_x =  _mm_set_ps1(0.0);
                tempZ_y =  _mm_set_ps1(0.0);
                tempZ_z =  _mm_set_ps1(0.0);
                //addition and multiplication of the 16 basis value and CP position for each axis
                for(a=0; a<16; a++){
                    tempX_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], xControlPointCoordinates.m[a]), tempX_x );
                    tempX_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], xControlPointCoordinates.m[a]), tempX_y );
                    tempX_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], xControlPointCoordinates.m[a]), tempX_z );

                    tempY_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], yControlPointCoordinates.m[a]), tempY_x );
                    tempY_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], yControlPointCoordinates.m[a]), tempY_y );
                    tempY_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], yControlPointCoordinates.m[a]), tempY_z );

                    tempZ_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], zControlPointCoordinates.m[a]), tempZ_x );
                    tempZ_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], zControlPointCoordinates.m[a]), tempZ_y );
                    tempZ_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], zControlPointCoordinates.m[a]), tempZ_z );
                }

                //the values stored in SSE variables are transfered to normal float
                val.m = tempX_x;
                Tx_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempX_y;
                Tx_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempX_z;
                Tx_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];

                val.m = tempY_x;
                Ty_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempY_y;
                Ty_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempY_z;
                Ty_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];

                val.m = tempZ_x;
                Tz_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempZ_y;
                Tz_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempZ_z;
                Tz_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];
#else
                for(a=0; a<64; a++){
                    Tx_x += basisX[a]*xControlPointCoordinates[a];
                    Tx_y += basisY[a]*xControlPointCoordinates[a];
                    Tx_z += basisZ[a]*xControlPointCoordinates[a];

                    Ty_x += basisX[a]*yControlPointCoordinates[a];
                    Ty_y += basisY[a]*yControlPointCoordinates[a];
                    Ty_z += basisZ[a]*yControlPointCoordinates[a];

                    Tz_x += basisX[a]*zControlPointCoordinates[a];
                    Tz_y += basisY[a]*zControlPointCoordinates[a];
                    Tz_z += basisZ[a]*zControlPointCoordinates[a];
                }
#endif

                jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
                jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
                jacobianMatrix.m[0][2]= (float)(Tx_z / realSpacing[2]);
                jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
                jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
                jacobianMatrix.m[1][2]= (float)(Ty_z / realSpacing[2]);
                jacobianMatrix.m[2][0]= (float)(Tz_x / realSpacing[0]);
                jacobianMatrix.m[2][1]= (float)(Tz_y / realSpacing[1]);
                jacobianMatrix.m[2][2]= (float)(Tz_z / realSpacing[2]);

                jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);

                if(jacobianDeterminant!=NULL)
                    jacobianDeterminant[index] = nifti_mat33_determ(jacobianMatrix);
                if(jacobianMatrices!=NULL)
                    jacobianMatrices[index] = jacobianMatrix;
                index++;
            }
        }
    }
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_computeJacobianMatricesFull_3D(nifti_image *referenceImage,
                                               nifti_image *splineControlPoint,
                                               mat33 *jacobianMatrices)
{
#if _USE_SSE
    if(sizeof(DTYPE)!=4){
        fprintf(stderr, "[NiftyReg ERROR] computeJacobianMatricesFull_3D\n");
        fprintf(stderr, "[NiftyReg ERROR] The SSE implementation assume single precision... Exit\n");
        exit(1);
    }
    union{
        __m128 m;
        float f[4];
    } val;
    __m128 tempX_x, tempX_y, tempX_z, tempY_x, tempY_y, tempY_z, tempZ_x, tempZ_y, tempZ_z;
#ifdef _WINDOWS
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisX;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisY;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} basisZ;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} xControlPointCoordinates;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} yControlPointCoordinates;
    union{__m128 m[16];__declspec(align(16)) DTYPE f[64];} zControlPointCoordinates;
#else // _WINDOWS
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisX;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisY;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} basisZ;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} xControlPointCoordinates;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} yControlPointCoordinates;
    union{__m128 m[16];DTYPE f[64] __attribute__((aligned(16)));} zControlPointCoordinates;
#endif // _WINDOWS
#else
    DTYPE basisX[64], basisY[64], basisZ[64];
    DTYPE xControlPointCoordinates[64], yControlPointCoordinates[64], zControlPointCoordinates[64];
#endif
    DTYPE yBasis[4], yFirst[4], xBasis[4], xFirst[4] ,zBasis[4] ,zFirst[4], basis;

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];
    DTYPE *controlPointPtrZ = &controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];

    mat44 refVox2GridVox_affine;
    if(referenceImage->sform_code>0){
        if(splineControlPoint->sform_code>0)
            refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->sto_ijk,&referenceImage->sto_xyz);
        else refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->qto_ijk,&referenceImage->sto_xyz);
    }
    else{
        if(splineControlPoint->sform_code>0)
            refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->sto_ijk,&referenceImage->qto_xyz);
        else refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->qto_ijk,&referenceImage->qto_xyz);
    }

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    int index, x, y, z, xPre, yPre, zPre, a, b, c, coord, oldXpre, oldYpre, oldZpre;
    DTYPE refPosition[3];
    DTYPE gridPosition[3];
    DTYPE Tx_x, Tx_y, Tx_z;
    DTYPE Ty_x, Ty_y, Ty_z;
    DTYPE Tz_x, Tz_y, Tz_z;
#ifdef _OPENMP
#ifdef _USE_SSE
#pragma omp parallel for default(none) \
    shared(referenceImage, splineControlPoint, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, reorient,\
    jacobianMatrices, realSpacing, refVox2GridVox_affine) \
    private(x, y, z, xPre, yPre, zPre, a, b, c, index, basis, val, \
    refPosition, gridPosition, basisX, basisY, basisZ, coord, \
    xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, oldXpre, oldYpre, oldZpre, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    Tx_x, Tx_y, Tx_z, Ty_x, Ty_y, Ty_z, Tz_x, Tz_y, Tz_z, jacobianMatrix, \
    tempX_x, tempX_y, tempX_z, tempY_x, tempY_y, tempY_z, tempZ_x, tempZ_y, tempZ_z)
#else // _USE_SEE
#pragma omp parallel for default(none) \
    shared(referenceImage, splineControlPoint, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, reorient,\
    jacobianMatrices, realSpacing, refVox2GridVox_affine) \
    private(x, y, z, xPre, yPre, zPre, a, b, c, index, basis, \
    basisX, basisY, basisZ, coord, refPosition, gridPosition, \
    xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, oldXpre, oldYpre, oldZpre, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates, \
    Tx_x, Tx_y, Tx_z, Ty_x, Ty_y, Ty_z, Tz_x, Tz_y, Tz_z, jacobianMatrix)
#endif // _USE_SEE
#endif // _USE_OPENMP
    for(z=0; z<referenceImage->nz; z++){
        oldXpre=999999; oldYpre=999999; oldZpre=999999;
        index=z*referenceImage->nx*referenceImage->ny;
        refPosition[2]=z;

        for(y=0; y<referenceImage->ny; y++){
            refPosition[1]=y;
            for(x=0; x<referenceImage->nx; x++){
                refPosition[0]=x;

                reg_mat44_mul<DTYPE>(&refVox2GridVox_affine,refPosition,gridPosition);

                xPre=static_cast<int>(floor(gridPosition[0]));
                basis=gridPosition[0]-(DTYPE)xPre;xPre--;
                if(basis<0.0) basis=0.0; //rounding error
                get_BSplineBasisValues<DTYPE>(basis, xBasis, xFirst);

                yPre=static_cast<int>(floor(gridPosition[1]));
                basis=gridPosition[1]-(DTYPE)yPre;yPre--;
                if(basis<0.0) basis=0.0; //rounding error
                get_BSplineBasisValues<DTYPE>(basis, yBasis, yFirst);

                zPre=static_cast<int>(floor(gridPosition[2]));
                basis=gridPosition[2]-(DTYPE)zPre;zPre--;
                if(basis<0.0) basis=0.0; //rounding error
                get_BSplineBasisValues<DTYPE>(basis, zBasis, zFirst);

                coord=0;
                for(c=0; c<4; c++){
                    for(b=0; b<4; b++){
                        Tx_x=yBasis[b]*zBasis[c];
                        Tx_y=yFirst[b]*zBasis[c];
                        Tx_z=yBasis[b]*zFirst[c];
                        for(a=0; a<4; a++){
#ifdef _USE_SSE
                            // That has to be improved
                            basisX.f[coord]=xFirst[a]*Tx_x;
                            basisY.f[coord]=xBasis[a]*Tx_y;
                            basisZ.f[coord]=xBasis[a]*Tx_z;
#else // _USE_SSE
                            basisX[coord]=xFirst[a]*Tx_x;   // z * y * x'
                            basisY[coord]=xBasis[a]*Tx_y;   // z * y' * x
                            basisZ[coord]=xBasis[a]*Tx_z;   // z' * y * x
#endif // _USE_SSE
                            coord++;
                        }
                    }
                }

                if(xPre!=oldXpre || yPre!=oldYpre || zPre!=oldZpre){
#ifdef _USE_SSE
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates.f,
                                          yControlPointCoordinates.f,
                                          zControlPointCoordinates.f,
                                          false, // no approx
                                          false // not disp
                                          );
#else // _USE_SSE
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates,
                                          yControlPointCoordinates,
                                          zControlPointCoordinates,
                                          false, // no approx
                                          false // not disp
                                          );
#endif // _USE_SSE
                    oldXpre=xPre; oldYpre=yPre; oldZpre=zPre;
                }

                Tx_x=0.0;
                Ty_x=0.0;
                Tz_x=0.0;
                Tx_y=0.0;
                Ty_y=0.0;
                Tz_y=0.0;
                Tx_z=0.0;
                Ty_z=0.0;
                Tz_z=0.0;

#ifdef _USE_SSE
                tempX_x =  _mm_set_ps1(0.0);
                tempX_y =  _mm_set_ps1(0.0);
                tempX_z =  _mm_set_ps1(0.0);
                tempY_x =  _mm_set_ps1(0.0);
                tempY_y =  _mm_set_ps1(0.0);
                tempY_z =  _mm_set_ps1(0.0);
                tempZ_x =  _mm_set_ps1(0.0);
                tempZ_y =  _mm_set_ps1(0.0);
                tempZ_z =  _mm_set_ps1(0.0);
                //addition and multiplication of the 16 basis value and CP position for each axis
                for(a=0; a<16; a++){
                    tempX_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], xControlPointCoordinates.m[a]), tempX_x );
                    tempX_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], xControlPointCoordinates.m[a]), tempX_y );
                    tempX_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], xControlPointCoordinates.m[a]), tempX_z );

                    tempY_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], yControlPointCoordinates.m[a]), tempY_x );
                    tempY_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], yControlPointCoordinates.m[a]), tempY_y );
                    tempY_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], yControlPointCoordinates.m[a]), tempY_z );

                    tempZ_x = _mm_add_ps(_mm_mul_ps(basisX.m[a], zControlPointCoordinates.m[a]), tempZ_x );
                    tempZ_y = _mm_add_ps(_mm_mul_ps(basisY.m[a], zControlPointCoordinates.m[a]), tempZ_y );
                    tempZ_z = _mm_add_ps(_mm_mul_ps(basisZ.m[a], zControlPointCoordinates.m[a]), tempZ_z );
                }

                //the values stored in SSE variables are transfered to normal float
                val.m = tempX_x;
                Tx_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempX_y;
                Tx_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempX_z;
                Tx_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];

                val.m = tempY_x;
                Ty_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempY_y;
                Ty_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempY_z;
                Ty_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];

                val.m = tempZ_x;
                Tz_x = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempZ_y;
                Tz_y = val.f[0]+val.f[1]+val.f[2]+val.f[3];
                val.m = tempZ_z;
                Tz_z = val.f[0]+val.f[1]+val.f[2]+val.f[3];
#else
                for(a=0; a<64; a++){
                    Tx_x += basisX[a]*xControlPointCoordinates[a];
                    Tx_y += basisY[a]*xControlPointCoordinates[a];
                    Tx_z += basisZ[a]*xControlPointCoordinates[a];

                    Ty_x += basisX[a]*yControlPointCoordinates[a];
                    Ty_y += basisY[a]*yControlPointCoordinates[a];
                    Ty_z += basisZ[a]*yControlPointCoordinates[a];

                    Tz_x += basisX[a]*zControlPointCoordinates[a];
                    Tz_y += basisY[a]*zControlPointCoordinates[a];
                    Tz_z += basisZ[a]*zControlPointCoordinates[a];
                }
#endif

                jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
                jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
                jacobianMatrix.m[0][2]= (float)(Tx_z / realSpacing[2]);
                jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
                jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
                jacobianMatrix.m[1][2]= (float)(Ty_z / realSpacing[2]);
                jacobianMatrix.m[2][0]= (float)(Tz_x / realSpacing[0]);
                jacobianMatrix.m[2][1]= (float)(Tz_y / realSpacing[1]);
                jacobianMatrix.m[2][2]= (float)(Tz_z / realSpacing[2]);

                jacobianMatrices[index] = jacobianMatrix;
                index++;
            } // x
        } // y
    } // x
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_computeJacobianMatricesFull_2D(nifti_image *referenceImage,
                                               nifti_image *splineControlPoint,
                                               mat33 *jacobianMatrices)
{
    DTYPE basisX[16], basisY[16];
    DTYPE xControlPointCoordinates[16], yControlPointCoordinates[16];
    DTYPE yBasis[4], yFirst[4], xBasis[4], xFirst[4], basis;

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny];

    mat44 refVox2GridVox_affine;
    if(referenceImage->sform_code>0){
        if(splineControlPoint->sform_code>0)
            refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->sto_ijk,&referenceImage->sto_xyz);
        else refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->qto_ijk,&referenceImage->sto_xyz);
    }
    else{
        if(splineControlPoint->sform_code>0)
            refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->sto_ijk,&referenceImage->qto_xyz);
        else refVox2GridVox_affine=reg_mat44_mul(&splineControlPoint->qto_ijk,&referenceImage->qto_xyz);
    }

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    int index, x, y, xPre, yPre, a, b, coord, oldXpre, oldYpre;
    DTYPE refPosition[3];
    DTYPE gridPosition[3];
    DTYPE Tx_x, Tx_y;
    DTYPE Ty_x, Ty_y;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(referenceImage, splineControlPoint, \
    controlPointPtrX, controlPointPtrY, reorient, \
    jacobianMatrices, realSpacing, refVox2GridVox_affine) \
    private(x, y, xPre, yPre, a, b, index, basis, \
    refPosition, gridPosition, basisX, basisY, coord, \
    xBasis, yBasis, xFirst, yFirst, oldXpre, oldYpre, \
    xControlPointCoordinates, yControlPointCoordinates, \
    Tx_x, Tx_y, Ty_x, Ty_y, jacobianMatrix)
#endif // _USE_OPENMP
    for(y=0; y<referenceImage->ny; y++){
        oldXpre=999999; oldYpre=999999;
        refPosition[1]=y;
        refPosition[2]=0;
        index=referenceImage->nx*y;
        for(x=0; x<referenceImage->nx; x++){
            refPosition[0]=x;

            reg_mat44_mul<DTYPE>(&refVox2GridVox_affine,refPosition,gridPosition);

            xPre=static_cast<int>(floor(gridPosition[0]));
            basis=gridPosition[0]-(DTYPE)xPre;xPre--;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, xBasis, xFirst);

            yPre=static_cast<int>(floor(gridPosition[1]));
            basis=gridPosition[1]-(DTYPE)yPre;yPre--;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, yBasis, yFirst);

            coord=0;
            for(b=0; b<4; b++){
                for(a=0; a<4; a++){
                    basisX[coord]=xFirst[a]*yBasis[b];   // y * x'
                    basisY[coord]=xBasis[a]*yFirst[b];   // y' * x
                    coord++;
                }
            }

            if(xPre!=oldXpre || yPre!=oldYpre){
                get_GridValues<DTYPE>(xPre,
                                      yPre,
                                      splineControlPoint,
                                      controlPointPtrX,
                                      controlPointPtrY,
                                      xControlPointCoordinates,
                                      yControlPointCoordinates,
                                      false, // no approx
                                      false // not disp
                                      );
                oldXpre=xPre; oldYpre=yPre;
            }

            Tx_x=0.0;
            Ty_x=0.0;
            Tx_y=0.0;
            Ty_y=0.0;

            for(a=0; a<16; a++){
                Tx_x += basisX[a]*xControlPointCoordinates[a];
                Tx_y += basisY[a]*xControlPointCoordinates[a];

                Ty_x += basisX[a]*yControlPointCoordinates[a];
                Ty_y += basisY[a]*yControlPointCoordinates[a];
            }

            jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
            jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
            jacobianMatrix.m[0][2]= 0;
            jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
            jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
            jacobianMatrix.m[1][2]= 0;
            jacobianMatrix.m[2][0]= 0;
            jacobianMatrix.m[2][1]= 0;
            jacobianMatrix.m[2][2]= 1;

            jacobianMatrices[index] = jacobianMatrix;
            index++;
        } // x
    } // y
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_computeApproximateJacobianMatrices_2D(nifti_image *splineControlPoint,
                                                      mat33 *jacobianMatrices,
                                                      DTYPE *jacobianDeterminant)
{
    unsigned int jacobianNumber = splineControlPoint->nx*splineControlPoint->ny;
    // As the contraint is only computed at the voxel position, the basis value of the spline are always the same
    DTYPE basisX[9], basisY[9], xControlPointCoordinates[9], yControlPointCoordinates[9];
    DTYPE normal[3]={1.0/6.0, 2.0/3.0, 1.0/6.0};
    DTYPE first[3]={-0.5, 0.0, 0.5};
    unsigned int coord=0;
    for(int b=0; b<3; b++){
        for(int a=0; a<3; a++){
            basisX[coord]=first[a]*normal[b];
            basisY[coord]=normal[a]*first[b];
            coord++;
        }
    }

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    jacobianMatrix.m[0][0]=jacobianMatrix.m[1][1]=jacobianMatrix.m[2][2]=1.f;
    jacobianMatrix.m[1][0]=jacobianMatrix.m[0][1]=jacobianMatrix.m[0][2]=0.f;
    jacobianMatrix.m[2][0]=jacobianMatrix.m[2][1]=jacobianMatrix.m[1][2]=0.f;
    for(unsigned int i=0;i<jacobianNumber;++i){
        memcpy(&jacobianMatrices[i], &jacobianMatrix, sizeof(mat33));
        jacobianDeterminant[i]=1.f;
    }

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[jacobianNumber];

    mat33 *jacobianMatricesPtr = jacobianMatrices;
    DTYPE *jacobianDeterminantPtr = jacobianDeterminant;

    /* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
    /* All the Jacobian matrices are computed */
    /* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
    int x, y, jacIndex, a;
    DTYPE Tx_x, Tx_y, Ty_x, Ty_y;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, controlPointPtrX, controlPointPtrY, realSpacing, \
    jacobianMatricesPtr, jacobianDeterminantPtr, reorient, basisX, basisY) \
    private(x, y, jacIndex, a, xControlPointCoordinates, yControlPointCoordinates, \
    Tx_x, Tx_y, Ty_x, Ty_y, jacobianMatrix)
#endif

    // Loop over (almost) each control point
    for(y=1;y<splineControlPoint->ny-1;y++){
        jacIndex = y*splineControlPoint->nx + 1;
        for(x=1;x<splineControlPoint->nx-1;x++){

            // The control points are stored
            get_GridValues<DTYPE>(x-1,
                                  y-1,
                                  splineControlPoint,
                                  controlPointPtrX,
                                  controlPointPtrY,
                                  xControlPointCoordinates,
                                  yControlPointCoordinates,
                                  true, // approx
                                  false // no disp
                                  );

            Tx_x=(DTYPE)0.0;
            Ty_x=(DTYPE)0.0;
            Tx_y=(DTYPE)0.0;
            Ty_y=(DTYPE)0.0;

            for(a=0; a<9; a++){
                Tx_x += basisX[a]*xControlPointCoordinates[a];
                Tx_y += basisY[a]*xControlPointCoordinates[a];
                Ty_x += basisX[a]*yControlPointCoordinates[a];
                Ty_y += basisY[a]*yControlPointCoordinates[a];
            }

            jacobianMatrix.m[0][0] = (float)(Tx_x / realSpacing[0]);
            jacobianMatrix.m[0][1] = (float)(Tx_y / realSpacing[1]);
            jacobianMatrix.m[0][2] = 0.0f;
            jacobianMatrix.m[1][0] = (float)(Ty_x / realSpacing[0]);
            jacobianMatrix.m[1][1] = (float)(Ty_y / realSpacing[1]);
            jacobianMatrix.m[1][2] = 0.0f;
            jacobianMatrix.m[2][0] = 0.0f;
            jacobianMatrix.m[2][1] = 0.0f;
            jacobianMatrix.m[2][2] = 1.0f;

            jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);

            jacobianDeterminantPtr[jacIndex] = (jacobianMatrix.m[0][0]*jacobianMatrix.m[1][1])
                    - (jacobianMatrix.m[0][1]*jacobianMatrix.m[1][0]);
            jacobianMatricesPtr[jacIndex] = jacobianMatrix;
            jacIndex++;
        } // x
    } // y
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_computeApproximateJacobianMatrices_3D(nifti_image *splineControlPoint,
                                                      mat33 *jacobianMatrices,
                                                      DTYPE *jacobianDeterminant)
{
    // As the contraint is only computed at the voxel position, the basis values of the spline are always the same
    float basisX[27], basisY[27], basisZ[27];
    DTYPE xControlPointCoordinates[27], yControlPointCoordinates[27], zControlPointCoordinates[27];
    DTYPE normal[3]={1.0/6.0, 2.0/3.0, 1.0/6.0};
    DTYPE first[3]={-0.5, 0, 0.5};
    // There are six different values taken into account
    DTYPE tempX[9], tempY[9], tempZ[9];
    int coord=0;
    for(int c=0; c<3; c++){
        for(int b=0; b<3; b++){
            tempX[coord]=normal[c]*normal[b];  // z * y
            tempY[coord]=normal[c]*first[b];  // z * y"
            tempZ[coord]=first[c]*normal[b];  // z"* y
            coord++;
        }
    }
    coord=0;
    for(int bc=0; bc<9; bc++){
        for(int a=0; a<3; a++){
            basisX[coord]=tempX[bc]*first[a];    // z * y * x"
            basisY[coord]=tempY[bc]*normal[a];    // z * y"* x
            basisZ[coord]=tempZ[bc]*normal[a];    // z"* y * x
            coord++;
        }
    }

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    jacobianMatrix.m[0][0]=jacobianMatrix.m[1][1]=jacobianMatrix.m[2][2]=1.f;
    jacobianMatrix.m[1][0]=jacobianMatrix.m[0][1]=jacobianMatrix.m[0][2]=0.f;
    jacobianMatrix.m[2][0]=jacobianMatrix.m[2][1]=jacobianMatrix.m[1][2]=0.f;
    for(unsigned int i=0;i<splineControlPoint->nvox/3;++i){
        jacobianMatrices[i]=jacobianMatrix;
        jacobianDeterminant[i]=1.f;
    }

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];
    DTYPE *controlPointPtrZ = &controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];

    /* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
    /* All the Jacobian matrices are computed */
    /* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */

    mat33 *jacobianMatricesPtr = jacobianMatrices;
    DTYPE *jacobianDeterminantPtr = jacobianDeterminant;

    DTYPE Tx_x; DTYPE Ty_x; DTYPE Tz_x;
    DTYPE Tx_y; DTYPE Ty_y; DTYPE Tz_y;
    DTYPE Tx_z; DTYPE Ty_z; DTYPE Tz_z;

    int x, y, z, a, jacIndex;
    // Loop over (almost) each control point
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, controlPointPtrX, controlPointPtrY, controlPointPtrZ, \
    basisX, basisY, basisZ, reorient, jacobianMatricesPtr, jacobianDeterminantPtr, realSpacing) \
    private(x,y,z,jacIndex, Tx_x, Ty_x, Tz_x, Tx_y, Ty_y, Tz_y, Tx_z, Ty_z, Tz_z, \
    jacobianMatrix, a, \
    xControlPointCoordinates, yControlPointCoordinates, zControlPointCoordinates)
#endif
    for(z=1;z<splineControlPoint->nz-1;z++){
        for(y=1;y<splineControlPoint->ny-1;y++){
            jacIndex = (z*splineControlPoint->ny+y)*splineControlPoint->nx+1;
            for(x=1;x<splineControlPoint->nx-1;x++){

                // The control points are stored
                get_GridValues<DTYPE>(x-1,
                                      y-1,
                                      z-1,
                                      splineControlPoint,
                                      controlPointPtrX,
                                      controlPointPtrY,
                                      controlPointPtrZ,
                                      xControlPointCoordinates,
                                      yControlPointCoordinates,
                                      zControlPointCoordinates,
                                      true, // approx
                                      false // not disp
                                      );

                Tx_x=(DTYPE)0.0; Ty_x=(DTYPE)0.0; Tz_x=(DTYPE)0.0;
                Tx_y=(DTYPE)0.0; Ty_y=(DTYPE)0.0; Tz_y=(DTYPE)0.0;
                Tx_z=(DTYPE)0.0; Ty_z=(DTYPE)0.0; Tz_z=(DTYPE)0.0;

                for(a=0; a<27; a++){
                    Tx_x += basisX[a]*xControlPointCoordinates[a];
                    Tx_y += basisY[a]*xControlPointCoordinates[a];
                    Tx_z += basisZ[a]*xControlPointCoordinates[a];
                    Ty_x += basisX[a]*yControlPointCoordinates[a];
                    Ty_y += basisY[a]*yControlPointCoordinates[a];
                    Ty_z += basisZ[a]*yControlPointCoordinates[a];
                    Tz_x += basisX[a]*zControlPointCoordinates[a];
                    Tz_y += basisY[a]*zControlPointCoordinates[a];
                    Tz_z += basisZ[a]*zControlPointCoordinates[a];
                }

                jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
                jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
                jacobianMatrix.m[0][2]= (float)(Tx_z / realSpacing[2]);
                jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
                jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
                jacobianMatrix.m[1][2]= (float)(Ty_z / realSpacing[2]);
                jacobianMatrix.m[2][0]= (float)(Tz_x / realSpacing[0]);
                jacobianMatrix.m[2][1]= (float)(Tz_y / realSpacing[1]);
                jacobianMatrix.m[2][2]= (float)(Tz_z / realSpacing[2]);

                jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
                jacobianDeterminantPtr[jacIndex] = nifti_mat33_determ(jacobianMatrix);
                jacobianMatricesPtr[jacIndex] = jacobianMatrix;
                jacIndex++;
            } // x
        } // y
    } //z
}
/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
void reg_spline_jacobianDeterminantGradient2D(nifti_image *splineControlPoint,
                                              nifti_image *referenceImage,
                                              nifti_image *gradientImage,
                                              float weight)
{
    mat33 *jacobianMatrices=(mat33 *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(DTYPE));

    reg_spline_computeJacobianMatrices_2D<DTYPE>(referenceImage,
                                                 splineControlPoint,
                                                 jacobianMatrices,
                                                 jacobianDeterminant);

    DTYPE gridVoxelSpacing[2];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;

    DTYPE basisValues[2], jacobianConstraint[2], detJac;
    DTYPE xBasis, yBasis, basis, xFirst, yFirst;

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    // The gradient are now computed for every control point
    DTYPE *gradientImagePtrX = static_cast<DTYPE *>(gradientImage->data);
    DTYPE *gradientImagePtrY = &gradientImagePtrX[gradientImage->nx*gradientImage->ny];

    int jacIndex, x, y, index, pixelX, pixelY, xPre, yPre;

    // Normalisation factor is added to the penalty term weight
    DTYPE ratio[2] = {
        referenceImage->dx * weight / ((DTYPE)(referenceImage->nx*referenceImage->ny)*splineControlPoint->dx),
        referenceImage->dy * weight / ((DTYPE)(referenceImage->nx*referenceImage->ny)*splineControlPoint->dy)
    };

#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, gridVoxelSpacing, jacobianDeterminant, jacobianMatrices, \
    referenceImage, desorient, gradientImagePtrX, gradientImagePtrY, ratio) \
    private(jacIndex, x, y, index, pixelX, pixelY, xPre, yPre, basisValues, basis, \
    jacobianConstraint, detJac, xBasis, xFirst, yBasis, yFirst, jacobianMatrix)
#endif
    for(y=0;y<splineControlPoint->ny;y++){
        index=y*splineControlPoint->nx;
        for(x=0;x<splineControlPoint->nx;x++){

            jacobianConstraint[0]=jacobianConstraint[1]=0;

            // Loop over all the control points in the surrounding area
            for(pixelY=(int)reg_ceil((y-3)*gridVoxelSpacing[1]);pixelY<(int)reg_ceil((y+1)*gridVoxelSpacing[1]); ++pixelY){
                if(pixelY>-1 && pixelY<referenceImage->ny){

                    yPre=(int)((DTYPE)pixelY/gridVoxelSpacing[1]);
                    basis=(DTYPE)pixelY/gridVoxelSpacing[1]-(DTYPE)yPre;
                    get_BSplineBasisValue<DTYPE>(basis,y-yPre,yBasis,yFirst);
                    if(yBasis!=0||yFirst!=0){

                        for(pixelX=(int)reg_ceil((x-3)*gridVoxelSpacing[0]);pixelX<(int)reg_ceil((x+1)*gridVoxelSpacing[0]); ++pixelX){
                            if(pixelX>-1 && pixelX<referenceImage->nx){

                                xPre=(int)((DTYPE)pixelX/gridVoxelSpacing[0]);
                                basis=(DTYPE)pixelX/gridVoxelSpacing[0]-(DTYPE)xPre;
                                get_BSplineBasisValue<DTYPE>(basis,x-xPre,xBasis,xFirst);

                                jacIndex = pixelY*referenceImage->nx+pixelX;
                                detJac=jacobianDeterminant[jacIndex];

                                if(detJac>0.0 && (xBasis!=0||xFirst!=0)){

                                    jacobianMatrix = jacobianMatrices[jacIndex];
                                    basisValues[0]= xFirst * yBasis;
                                    basisValues[1]= xBasis * yFirst;
#ifdef _USE_SQUARE_LOG_JAC
                                    detJac= 2.0*log(detJac) /detJac;
#else
                                    detJac = (log(detJac)>0?1.0:-1.0) / detjac;
#endif
                                    addJacobianGradientValues(jacobianMatrix,
                                                              detJac,
                                                              basisValues[0],
                                                              basisValues[1],
                                                              jacobianConstraint);
                                }
                            }// if x
                        }// x
                    }
                }// if y
            }// y
            gradientImagePtrX[index] += ratio[0] *
                    (desorient.m[0][0]*jacobianConstraint[0] +
                     desorient.m[0][1]*jacobianConstraint[1]);
            gradientImagePtrY[index] += ratio[1] *
                    (desorient.m[1][0]*jacobianConstraint[0] +
                     desorient.m[1][1]*jacobianConstraint[1]);
            index++;
        }
    }
    free(jacobianDeterminant);
    free(jacobianMatrices);

}
/* *************************************************************** */
template<class DTYPE>
void reg_spline_jacobianDeterminantGradientApprox2D(nifti_image *splineControlPoint,
                                                    nifti_image *referenceImage,
                                                    nifti_image *gradientImage,
                                                    float weight
                                                    )
{
    unsigned int jacobianNumber = splineControlPoint->nx * splineControlPoint->ny;

    mat33 *jacobianMatrices=(mat33 *)malloc(jacobianNumber * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc(jacobianNumber * sizeof(DTYPE));
    reg_spline_computeApproximateJacobianMatrices_2D<DTYPE>(splineControlPoint,
                                                            jacobianMatrices,
                                                            jacobianDeterminant);


    DTYPE basisX[9], basisY[9], detJac, jacobianConstraint[2];
    DTYPE normal[3]={1.0/6.0, 2.0/3.0, 1.0/6.0};
    DTYPE first[3]={-0.5, 0.0, 0.5};
    unsigned int coord=0;
    // INVERTED ON PURPOSE
    for(int b=2; b>-1; --b){
        for(int a=2; a>-1; --a){
            basisX[coord]=first[a]*normal[b];
            basisY[coord]=normal[a]*first[b];
            coord++;
        }
    }

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    DTYPE *gradientImagePtrX = static_cast<DTYPE *>(gradientImage->data);
    DTYPE *gradientImagePtrY = &gradientImagePtrX[gradientImage->nx*gradientImage->ny];

    DTYPE approxRatio[2] = {
        referenceImage->dx*weight / ((DTYPE)(jacobianNumber*splineControlPoint->dx)),
        referenceImage->dy*weight / ((DTYPE)(jacobianNumber*splineControlPoint->dy))};

    int jacIndex, index, x, y, pixelX, pixelY;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, jacobianMatrices, jacobianDeterminant, basisX, basisY, \
    gradientImagePtrX, gradientImagePtrY, desorient, approxRatio) \
    private(jacIndex, index, x, y, pixelX, pixelY, jacobianMatrix, \
    detJac, jacobianConstraint, coord)
#endif
    for(y=0;y<splineControlPoint->ny;y++){
        index=y*splineControlPoint->nx;
        for(x=0;x<splineControlPoint->nx;x++){

            jacobianConstraint[0]=jacobianConstraint[1]=0;

            // Loop over all the control points in the surrounding area
            coord=0;
            for(pixelY=(int)(y-1);pixelY<(int)(y+2); ++pixelY){
                if(pixelY>-1 && pixelY<splineControlPoint->ny){

                    for(pixelX=(int)(x-1);pixelX<(int)(x+2); ++pixelX){
                        if(pixelX>-1 && pixelX<splineControlPoint->nx){

                            jacIndex = pixelY*splineControlPoint->nx+pixelX;
                            detJac=jacobianDeterminant[jacIndex];

                            if(detJac>0.0){

                                jacobianMatrix = jacobianMatrices[jacIndex];

#ifdef _USE_SQUARE_LOG_JAC
                                /* derivative of the squared log of the Jacobian determinant */
                                detJac = 2.0 * log(detJac) / detJac;
#else
                                detJac = (log(detJac)>0?1.0:-1.0) / detJac;
#endif
                                addJacobianGradientValues(jacobianMatrix,
                                                          detJac,
                                                          basisX[coord],
                                                          basisY[coord],
                                                          jacobianConstraint);
                            }
                        } // if x
                        coord++;
                    }// x
                }// if y
                else coord+=3;
            }// y
            // (Marc) I removed the normalisation by the voxel number as each gradient has to be normalised in the same way (NMI, BE, JAC)
            gradientImagePtrX[index] += approxRatio[0] * (desorient.m[0][0]*jacobianConstraint[0] + desorient.m[0][1]*jacobianConstraint[1]);
            gradientImagePtrY[index] += approxRatio[1] * (desorient.m[1][0]*jacobianConstraint[0] + desorient.m[1][1]*jacobianConstraint[1]);
            index++;
        }
    }
    free(jacobianMatrices);
    free(jacobianDeterminant);
}
/* *************************************************************** */
template<class DTYPE>
void reg_spline_jacobianDeterminantGradient3D( nifti_image *splineControlPoint,
                                               nifti_image *referenceImage,
                                               nifti_image *gradientImage,
                                               float weight)
{
    mat33 *jacobianMatrices=(mat33 *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(DTYPE));

    reg_spline_computeJacobianMatrices_3D<DTYPE>(referenceImage,
                                                 splineControlPoint,
                                                 jacobianMatrices,
                                                 jacobianDeterminant);

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    DTYPE gridVoxelSpacing[3];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;
    gridVoxelSpacing[2] = splineControlPoint->dz / referenceImage->dz;

    DTYPE xBasis, yBasis, zBasis, basis;
    DTYPE xFirst, yFirst, zFirst;
    DTYPE basisValues[3];
    unsigned int jacIndex;

    // The gradient are now computed for every control point
    DTYPE *gradientImagePtrX = static_cast<DTYPE *>(gradientImage->data);
    DTYPE *gradientImagePtrY = &gradientImagePtrX[gradientImage->nx*gradientImage->ny*gradientImage->nz];
    DTYPE *gradientImagePtrZ = &gradientImagePtrY[gradientImage->nx*gradientImage->ny*gradientImage->nz];

    // Normalisation factor is added to the penalty term weight
    DTYPE ratio[3] = {
        referenceImage->dx * weight / ((DTYPE)(referenceImage->nx*referenceImage->ny*referenceImage->nz)*splineControlPoint->dx),
        referenceImage->dy * weight / ((DTYPE)(referenceImage->nx*referenceImage->ny*referenceImage->nz)*splineControlPoint->dy),
        referenceImage->dz * weight / ((DTYPE)(referenceImage->nx*referenceImage->ny*referenceImage->nz)*splineControlPoint->dz)
    };

    int x, y, z, xPre, yPre, zPre, pixelX, pixelY, pixelZ, index;
    DTYPE jacobianConstraint[3];
    double detJac;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, gridVoxelSpacing, referenceImage, jacobianDeterminant, ratio, \
    jacobianMatrices, gradientImagePtrX, gradientImagePtrY, gradientImagePtrZ, desorient) \
    private(x, y, z, xPre, yPre, zPre, pixelX, pixelY, pixelZ, jacobianConstraint, \
    basis, xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, jacIndex, index, detJac, \
    jacobianMatrix, basisValues)
#endif
    for(z=0;z<splineControlPoint->nz;z++){
        index=z*splineControlPoint->nx*splineControlPoint->ny;
        for(y=0;y<splineControlPoint->ny;y++){
            for(x=0;x<splineControlPoint->nx;x++){

                jacobianConstraint[0]=jacobianConstraint[1]=jacobianConstraint[2]=0.;

                // Loop over all the control points in the surrounding area
                for(pixelZ=(int)reg_ceil((z-3)*gridVoxelSpacing[2]);pixelZ<=(int)reg_ceil((z+1)*gridVoxelSpacing[2]); pixelZ++){
                    if(pixelZ>-1 && pixelZ<referenceImage->nz){

                        zPre=(int)((DTYPE)pixelZ/gridVoxelSpacing[2]);
                        basis=(DTYPE)pixelZ/gridVoxelSpacing[2]-(DTYPE)zPre;
                        get_BSplineBasisValue<DTYPE>(basis,z-zPre,zBasis,zFirst);

                        for(pixelY=(int)reg_ceil((y-3)*gridVoxelSpacing[1]);pixelY<=(int)reg_ceil((y+1)*gridVoxelSpacing[1]); pixelY++){
                            if(pixelY>-1 && pixelY<referenceImage->ny && (zFirst!=0 || zBasis!=0)){

                                yPre=(int)((DTYPE)pixelY/gridVoxelSpacing[1]);
                                basis=(DTYPE)pixelY/gridVoxelSpacing[1]-(DTYPE)yPre;
                                get_BSplineBasisValue<DTYPE>(basis,y-yPre,yBasis,yFirst);

                                jacIndex = (pixelZ*referenceImage->ny+pixelY)*referenceImage->nx+(int)reg_ceil((x-3)*gridVoxelSpacing[0]);

                                for(pixelX=(int)reg_ceil((x-3)*gridVoxelSpacing[0]);pixelX<=(int)reg_ceil((x+1)*gridVoxelSpacing[0]); pixelX++){
                                    if(pixelX>-1 && pixelX<referenceImage->nx && (yFirst!=0 || yBasis!=0)){

                                        detJac = jacobianDeterminant[jacIndex];

                                        xPre=(int)((DTYPE)pixelX/gridVoxelSpacing[0]);
                                        basis=(DTYPE)pixelX/gridVoxelSpacing[0]-(DTYPE)xPre;
                                        get_BSplineBasisValue<DTYPE>(basis,x-xPre,xBasis,xFirst);

                                        if(detJac>0.0 && (xBasis!=0 ||xFirst!=0)){

                                            jacobianMatrix = jacobianMatrices[jacIndex];

                                            basisValues[0] = xFirst * yBasis * zBasis ;
                                            basisValues[1] = xBasis * yFirst * zBasis ;
                                            basisValues[2] = xBasis * yBasis * zFirst ;

                                            jacobianMatrix = jacobianMatrices[jacIndex];
#ifdef _USE_SQUARE_LOG_JAC
                                            detJac= 2.0*log(detJac) / detJac;
#else
                                            detJac = (log(detJac)>0?1.0:-1.0) / detJac;
#endif
                                            addJacobianGradientValues<DTYPE>(jacobianMatrix,
                                                                             detJac,
                                                                             basisValues[0],
                                                                             basisValues[1],
                                                                             basisValues[2],
                                                                             jacobianConstraint);
                                        }
                                    } // if x
                                    jacIndex++;
                                }// x
                            }// if y
                        }// y
                    }// if z
                } // z
                // (Marc) I removed the normalisation by the voxel number as each gradient has to be normalised in the same way (NMI, BE, JAC)
                gradientImagePtrX[index] += ratio[0] *
                        ( desorient.m[0][0]*jacobianConstraint[0]
                          + desorient.m[0][1]*jacobianConstraint[1]
                          + desorient.m[0][2]*jacobianConstraint[2]);
                gradientImagePtrY[index] += ratio[1] *
                        ( desorient.m[1][0]*jacobianConstraint[0]
                          + desorient.m[1][1]*jacobianConstraint[1]
                          + desorient.m[1][2]*jacobianConstraint[2]);
                gradientImagePtrZ[index] += ratio[2] *
                        ( desorient.m[2][0]*jacobianConstraint[0]
                          + desorient.m[2][1]*jacobianConstraint[1]
                          + desorient.m[2][2]*jacobianConstraint[2]);
                index++;
            }
        }
    }
    free(jacobianMatrices);
    free(jacobianDeterminant);
}
/* *************************************************************** */
template<class DTYPE>
void reg_spline_jacobianDeterminantGradientApprox3D(nifti_image *splineControlPoint,
                                                    nifti_image *referenceImage,
                                                    nifti_image *gradientImage,
                                                    float weight)
{

    unsigned int jacobianNumber = splineControlPoint->nx * splineControlPoint->ny * splineControlPoint->nz;

    mat33 *jacobianMatrices=(mat33 *)malloc(jacobianNumber * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc(jacobianNumber * sizeof(DTYPE));

    reg_spline_computeApproximateJacobianMatrices_3D<DTYPE>(splineControlPoint,
                                                            jacobianMatrices,
                                                            jacobianDeterminant);

    DTYPE basisX[27], basisY[27], basisZ[27];
    DTYPE normal[3]={1.0/6.0, 2.0/3.0, 1.0/6.0};
    DTYPE first[3]={-0.5, 0.0, 0.5};
    unsigned int coord=0;
    // INVERTED ON PURPOSE
    for(int c=2; c>-1; --c){
        for(int b=2; b>-1; --b){
            for(int a=2; a>-1; --a){
                basisX[coord]=normal[c]*normal[b]*first[a];
                basisY[coord]=normal[c]*first[b]*normal[a];
                basisZ[coord]=first[c]*normal[b]*normal[a];
                coord++;
            }
        }
    }

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    DTYPE *gradientImagePtrX = static_cast<DTYPE *>(gradientImage->data);
    DTYPE *gradientImagePtrY = &gradientImagePtrX[jacobianNumber];
    DTYPE *gradientImagePtrZ = &gradientImagePtrY[jacobianNumber];

    DTYPE approxRatio[3] = {
        referenceImage->dx*weight / ((DTYPE)jacobianNumber*splineControlPoint->dx),
        referenceImage->dy*weight / ((DTYPE)jacobianNumber*splineControlPoint->dy),
        referenceImage->dz*weight / ((DTYPE)jacobianNumber*splineControlPoint->dz)};

    int x, y, z, jacIndex, pixelX, pixelY, pixelZ, index;
    DTYPE jacobianConstraint[3];
    double detJac;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, jacobianMatrices, jacobianDeterminant, basisX, basisY, basisZ, \
    approxRatio, gradientImagePtrX, gradientImagePtrY, gradientImagePtrZ, desorient) \
    private(x, y, z, index, jacobianConstraint, pixelX, pixelY, pixelZ, jacIndex, coord, \
    detJac, jacobianMatrix)
#endif
    for(z=0;z<splineControlPoint->nz;z++){
        index=z*splineControlPoint->nx*splineControlPoint->ny;
        for(y=0;y<splineControlPoint->ny;y++){
            for(x=0;x<splineControlPoint->nx;x++){

                jacobianConstraint[0]=jacobianConstraint[1]=jacobianConstraint[2]=0;

                // Loop over all the control points in the surrounding area
                coord=0;
                for(pixelZ=(int)(z-1); pixelZ<(int)(z+2); ++pixelZ){
                    if(pixelZ>0 && pixelZ<splineControlPoint->nz-1){

                        for(pixelY=(int)(y-1); pixelY<(int)(y+2); ++pixelY){
                            if(pixelY>0 && pixelY<splineControlPoint->ny-1){

                                jacIndex = (pixelZ*splineControlPoint->ny+pixelY)*splineControlPoint->nx+x-1;
                                for(pixelX=(int)(x-1); pixelX<(int)(x+2); ++pixelX){
                                    if(pixelX>0 && pixelX<splineControlPoint->nx-1){

                                        detJac = (double)jacobianDeterminant[jacIndex];

                                        if(detJac>0.0){
                                            jacobianMatrix = jacobianMatrices[jacIndex];
#ifdef _USE_SQUARE_LOG_JAC
                                            detJac = 2.0*log(detJac) / detJac;
#else
                                            detJac = (log(detJac)>0?1.0:-1.0) / detJac;
#endif
                                            addJacobianGradientValues<DTYPE>(jacobianMatrix,
                                                                             detJac,
                                                                             basisX[coord],
                                                                             basisY[coord],
                                                                             basisZ[coord],
                                                                             jacobianConstraint);
                                        }
                                    } // if x
                                    coord++;
                                    jacIndex++;
                                }// x
                            }// if y
                            else coord+=3;
                        }// y
                    }// if z
                    else coord+=9;
                } // z
                // (Marc) I removed the normalisation by the voxel number as each gradient has to be normalised in the same way (NMI, BE, JAC)
                gradientImagePtrX[index] += approxRatio[0] *
                        ( desorient.m[0][0]*jacobianConstraint[0]
                          + desorient.m[0][1]*jacobianConstraint[1]
                          + desorient.m[0][2]*jacobianConstraint[2]);
                gradientImagePtrY[index] += approxRatio[1] *
                        ( desorient.m[1][0]*jacobianConstraint[0]
                          + desorient.m[1][1]*jacobianConstraint[1]
                          + desorient.m[1][2]*jacobianConstraint[2]);
                gradientImagePtrZ[index] += approxRatio[2] *
                        ( desorient.m[2][0]*jacobianConstraint[0]
                          + desorient.m[2][1]*jacobianConstraint[1]
                          + desorient.m[2][2]*jacobianConstraint[2]);
                index++;
            }
        }
    }
    free(jacobianMatrices);
    free(jacobianDeterminant);
}
/* *************************************************************** */
extern "C++"
void reg_spline_getJacobianPenaltyTermGradient(nifti_image *splineControlPoint,
                                               nifti_image *referenceImage,
                                               nifti_image *gradientImage,
                                               float weight,
                                               bool approx)
{
    if(splineControlPoint->datatype != gradientImage->datatype){
        fprintf(stderr,"[NiftyReg ERROR] The spline control point image and the gradient image were expected to have the same datatype\n");
        fprintf(stderr,"[NiftyReg ERROR] The bending energy gradient has not computed\n");
        exit(1);
    }

    if(splineControlPoint->nz==1){
        if(approx){
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                reg_spline_jacobianDeterminantGradientApprox2D<float>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            case NIFTI_TYPE_FLOAT64:
                reg_spline_jacobianDeterminantGradientApprox2D<double>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The jacobian penalty gradient has not computed\n");
                exit(1);
            }
        }
        else{
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                reg_spline_jacobianDeterminantGradient2D<float>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            case NIFTI_TYPE_FLOAT64:
                reg_spline_jacobianDeterminantGradient2D<double>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The jacobian penalty gradient has not computed\n");
                exit(1);
            }
        }
    }
    else{
        if(approx){
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                reg_spline_jacobianDeterminantGradientApprox3D<float>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            case NIFTI_TYPE_FLOAT64:
                reg_spline_jacobianDeterminantGradientApprox3D<double>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The jacobian penalty gradient has not computed\n");
                exit(1);
            }
        }
        else{
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                reg_spline_jacobianDeterminantGradient3D<float>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            case NIFTI_TYPE_FLOAT64:
                reg_spline_jacobianDeterminantGradient3D<double>
                        (splineControlPoint, referenceImage, gradientImage, weight);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The jacobian penalty gradient has not computed\n");
                exit(1);
            }
        }
    }
}
/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
double reg_spline_correctFolding_2D(nifti_image *splineControlPoint,
                                    nifti_image *referenceImage)
{

    mat33 *jacobianMatrices=(mat33 *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(DTYPE));

    reg_spline_computeJacobianMatrices_2D<DTYPE>(referenceImage,
                                                 splineControlPoint,
                                                 jacobianMatrices,
                                                 jacobianDeterminant);

    // The current Penalty term value is computed
    double penaltyTerm =0.0, logDet;
    int i;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(referenceImage, jacobianDeterminant) \
    private(i, logDet) \
    reduction(+:penaltyTerm)
#endif
    for(i=0; i< (referenceImage->nx*referenceImage->ny*referenceImage->nz); i++){
        logDet = log(jacobianDeterminant[i]);
#ifdef _USE_SQUARE_LOG_JAC
        penaltyTerm += logDet*logDet;
#else
        penaltyTerm +=  fabs(log(logDet));
#endif
    }
    if(penaltyTerm==penaltyTerm){
        free(jacobianDeterminant);
        free(jacobianMatrices);
        return penaltyTerm/(double)(referenceImage->nx*referenceImage->ny*referenceImage->nz);
    }

    DTYPE gridVoxelSpacing[2];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;

    DTYPE basisValues[2], gradient[2], norm;
    DTYPE xBasis=0, yBasis=0, basis, foldingCorrection[2];
    DTYPE xFirst=0, yFirst=0;
    int jacIndex, id, x, y, pixelX, pixelY, xPre, yPre;
    bool correctFolding;
    double detJac;

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    // The gradient are now computed for every control point
    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny];

#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, gridVoxelSpacing, jacobianDeterminant, jacobianMatrices, \
    referenceImage, desorient, controlPointPtrX, controlPointPtrY, realSpacing) \
    private(x, y, pixelX, pixelY, jacIndex, detJac, xPre, yPre, xBasis, xFirst, yBasis, yFirst, \
    basisValues, correctFolding, jacobianMatrix, gradient, norm, id, foldingCorrection, basis)
#endif
    for(y=0;y<splineControlPoint->ny;y++){
        for(x=0;x<splineControlPoint->nx;x++){

            foldingCorrection[0]=foldingCorrection[1]=0;

            correctFolding=false;

            // Loop over all the control points in the surrounding area
            for(pixelY=(int)reg_ceil((y-3)*gridVoxelSpacing[1]);pixelY<(int)reg_floor((y+1)*gridVoxelSpacing[1]); pixelY++){
                if(pixelY>-1 && pixelY<referenceImage->ny){

                    for(pixelX=(int)reg_ceil((x-3)*gridVoxelSpacing[0]);pixelX<(int)reg_floor((x+1)*gridVoxelSpacing[0]); pixelX++){
                        if(pixelX>-1 && pixelX<referenceImage->nx){

                            jacIndex = pixelY*referenceImage->nx+pixelX;
                            detJac=jacobianDeterminant[jacIndex];

                            if(detJac<=0.0){

                                yPre=(int)((DTYPE)pixelY/gridVoxelSpacing[1]);
                                basis=(DTYPE)pixelY/gridVoxelSpacing[1]-(DTYPE)yPre;
                                get_BSplineBasisValue<DTYPE>(basis, y-yPre,yBasis,yFirst);

                                xPre=(int)((DTYPE)pixelX/gridVoxelSpacing[0]);
                                basis=(DTYPE)pixelX/gridVoxelSpacing[0]-(DTYPE)xPre;
                                get_BSplineBasisValue<DTYPE>(basis, x-xPre,xBasis,xFirst);

                                basisValues[0]= xFirst * yBasis;
                                basisValues[1]= xBasis * yFirst;

                                jacobianMatrix = jacobianMatrices[jacIndex];

                                /* derivative of the Jacobian determinant itself */
                                correctFolding=true;
                                addJacobianGradientValues<DTYPE>(jacobianMatrix,
                                                                 1.0,
                                                                 basisValues[0],
                                                                 basisValues[1],
                                                                 foldingCorrection);
                            }
                        }// if x
                    }// x
                }// if y
            }// y
            // (Marc) I removed the normalisation by the voxel number as each gradient has to be normalised in the same way (NMI, BE, JAC)
            if(correctFolding){
                gradient[0] = desorient.m[0][0]*foldingCorrection[0] +
                        desorient.m[0][1]*foldingCorrection[1];
                gradient[1] = desorient.m[1][0]*foldingCorrection[0] +
                        desorient.m[1][1]*foldingCorrection[1];
                norm = 5.0 * sqrt(gradient[0]*gradient[0] + gradient[1]*gradient[1]);
                if(norm>0.0){
                    id = y*splineControlPoint->nx+x;
                    controlPointPtrX[id] += (DTYPE)(realSpacing[0]*gradient[0]/norm);
                    controlPointPtrY[id] += (DTYPE)(realSpacing[1]*gradient[1]/norm);
                }
            }

        }
    }
    free(jacobianDeterminant);
    free(jacobianMatrices);
    return std::numeric_limits<double>::quiet_NaN();

}
/* *************************************************************** */
template<class DTYPE>
double reg_spline_correctFoldingApprox_2D(nifti_image *splineControlPoint)
{

    unsigned int jacobianNumber = splineControlPoint->nx * splineControlPoint->ny;

    mat33 *jacobianMatrices=(mat33 *)malloc(jacobianNumber * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc(jacobianNumber * sizeof(DTYPE));

    reg_spline_computeApproximateJacobianMatrices_2D<DTYPE>(splineControlPoint,
                                                            jacobianMatrices,
                                                            jacobianDeterminant);

    // The current Penalty term value is computed
    int jacIndex, i, j;
    double penaltyTerm=0.0, logDet;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, jacobianDeterminant) \
    private(i, j, logDet, jacIndex) \
    reduction(+:penaltyTerm)
#endif
    for(j=1; j< splineControlPoint->ny-1; j++){
        jacIndex = j*splineControlPoint->nx+1;
        for(i=1; i< splineControlPoint->nx-1; i++){
            logDet = log(jacobianDeterminant[jacIndex++]);
#ifdef _USE_SQUARE_LOG_JAC
            penaltyTerm += logDet*logDet;
#else
            penaltyTerm +=  fabs(log(logDet));
#endif
        }
    }
    if(penaltyTerm==penaltyTerm){
        free(jacobianDeterminant);
        free(jacobianMatrices);
        jacobianNumber = (splineControlPoint->nx-2) * (splineControlPoint->ny-2);
        return penaltyTerm/(double)jacobianNumber;
    }

    DTYPE basisValues[2], foldingCorrection[2], gradient[2], norm;
    DTYPE xBasis=0, yBasis=0, xFirst=0, yFirst=0;

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    // The gradient are now computed for every control point
    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny];

    /* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
    /* The actual gradient are now computed */
    /* /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/ */
    int x, y, pixelX, pixelY, id;
    bool correctFolding;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, jacobianDeterminant, jacobianMatrices, desorient, \
    controlPointPtrX, controlPointPtrY, realSpacing) \
    private(x, y, id, jacIndex, pixelX, pixelY, logDet, xBasis, xFirst, yBasis, yFirst, \
    basisValues, foldingCorrection, correctFolding, gradient, jacobianMatrix, norm)
#endif
    for(y=0;y<splineControlPoint->ny;y++){
        for(x=0;x<splineControlPoint->nx;x++){

            foldingCorrection[0]=foldingCorrection[1]=0;
            correctFolding=false;

            // Loop over all the control points in the surrounding area
            for(pixelY=(y-1);pixelY<(y+2); pixelY++){
                if(pixelY>0 && pixelY<splineControlPoint->ny-1){

                    for(pixelX=(int)((x-1));pixelX<(int)((x+2)); pixelX++){
                        if(pixelX>0 && pixelX<splineControlPoint->nx-1){

                            jacIndex = pixelY*splineControlPoint->nx+pixelX;
                            logDet=jacobianDeterminant[jacIndex];

                            if(logDet<=0.0){

                                get_BSplineBasisValue<DTYPE>(0, y-pixelY+1,yBasis,yFirst);
                                get_BSplineBasisValue<DTYPE>(0, x-pixelX+1,xBasis,xFirst);

                                basisValues[0] = xFirst * yBasis ;
                                basisValues[1] = xBasis * yFirst ;

                                jacobianMatrix = jacobianMatrices[jacIndex];

                                /* derivative of the Jacobian determinant itself */
                                correctFolding=true;
                                addJacobianGradientValues<DTYPE>(jacobianMatrix,
                                                                 1.0,
                                                                 basisValues[0],
                                                                 basisValues[1],
                                                                 foldingCorrection);
                            }
                        }// if x
                    }// x
                }// if y
            }// y
            // (Marc) I removed the normalisation by the voxel number as each gradient has to be normalised in the same way (NMI, BE, JAC)
            if(correctFolding){
                gradient[0] = desorient.m[0][0]*foldingCorrection[0]
                        + desorient.m[0][1]*foldingCorrection[1];
                gradient[1] = desorient.m[1][0]*foldingCorrection[0]
                        + desorient.m[1][1]*foldingCorrection[1];
                norm = 5.0 * sqrt(gradient[0]*gradient[0] + gradient[1]*gradient[1]);
                if(norm>0.0){
                    id = y*splineControlPoint->nx+x;
                    controlPointPtrX[id] += realSpacing[0]*gradient[0]/norm;
                    controlPointPtrY[id] += realSpacing[1]*gradient[1]/norm;
                }
            }

        }
    }
    free(jacobianDeterminant);
    free(jacobianMatrices);
    return std::numeric_limits<double>::quiet_NaN();
}
/* *************************************************************** */
template<class DTYPE>
double reg_spline_correctFolding_3D(nifti_image *splineControlPoint,
                                    nifti_image *referenceImage)
{

    mat33 *jacobianMatrices=(mat33 *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc((referenceImage->nx*referenceImage->ny*referenceImage->nz) * sizeof(DTYPE));

    reg_spline_computeJacobianMatrices_3D<DTYPE>(referenceImage,
                                                 splineControlPoint,
                                                 jacobianMatrices,
                                                 jacobianDeterminant);

    /* The current Penalty term value is computed */
    double penaltyTerm =0.0;
    int i;
    double logDet;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(referenceImage, jacobianDeterminant) \
    private(i,logDet) \
    reduction(+:penaltyTerm)
#endif
    for(i=0; i< (referenceImage->nx*referenceImage->ny*referenceImage->nz); i++){
        logDet = log(jacobianDeterminant[i]);
#ifdef _USE_SQUARE_LOG_JAC
        penaltyTerm += logDet*logDet;
#else
        penaltyTerm +=  fabs(log(logDet));
#endif
    }
    if(penaltyTerm==penaltyTerm){
        free(jacobianDeterminant);
        free(jacobianMatrices);
        return penaltyTerm/(double)(referenceImage->nx*referenceImage->ny*referenceImage->nz);
    }

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];
    DTYPE *controlPointPtrZ = &controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];

    DTYPE gridVoxelSpacing[3];
    gridVoxelSpacing[0] = splineControlPoint->dx / referenceImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / referenceImage->dy;
    gridVoxelSpacing[2] = splineControlPoint->dz / referenceImage->dz;

    DTYPE basisValues[3], foldingCorrection[3], gradient[3], norm;
    DTYPE xBasis=0, yBasis=0, zBasis=0, basis, xFirst=0, yFirst=0, zFirst=0;
    int jacIndex, x, y, z, id, pixelX, pixelY, pixelZ, xPre, yPre, zPre;
    bool correctFolding;
    double detJac;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, gridVoxelSpacing, referenceImage, jacobianDeterminant, realSpacing, \
    jacobianMatrices, controlPointPtrX, controlPointPtrY, controlPointPtrZ, desorient) \
    private(x, y, z, xPre, yPre, zPre, pixelX, pixelY, pixelZ, foldingCorrection, \
    basis, xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, jacIndex, detJac, \
    jacobianMatrix, basisValues, norm, correctFolding, id, gradient)
#endif
    for(z=0;z<splineControlPoint->nz;z++){
        for(y=0;y<splineControlPoint->ny;y++){
            for(x=0;x<splineControlPoint->nx;x++){

                foldingCorrection[0]=foldingCorrection[1]=foldingCorrection[2]=0;
                correctFolding=false;

                // Loop over all the control points in the surrounding area
                for(pixelZ=(int)reg_ceil((z-3)*gridVoxelSpacing[2]);pixelZ<(int)reg_floor((z+1)*gridVoxelSpacing[2]); pixelZ++){
                    if(pixelZ>-1 && pixelZ<referenceImage->nz){

                        for(pixelY=(int)reg_ceil((y-3)*gridVoxelSpacing[1]);pixelY<(int)reg_floor((y+1)*gridVoxelSpacing[1]); pixelY++){
                            if(pixelY>-1 && pixelY<referenceImage->ny){

                                for(pixelX=(int)reg_ceil((x-3)*gridVoxelSpacing[0]);pixelX<(int)reg_floor((x+1)*gridVoxelSpacing[0]); pixelX++){
                                    if(pixelX>-1 && pixelX<referenceImage->nx){

                                        jacIndex = (pixelZ*referenceImage->ny+pixelY)*referenceImage->nx+pixelX;
                                        detJac = jacobianDeterminant[jacIndex];

                                        if(detJac<=0.0){

                                            jacobianMatrix = jacobianMatrices[jacIndex];

                                            zPre=(int)((DTYPE)pixelZ/gridVoxelSpacing[2]);
                                            basis=(DTYPE)pixelZ/gridVoxelSpacing[2]-(DTYPE)zPre;
                                            get_BSplineBasisValue<DTYPE>(basis, z-zPre,zBasis,zFirst);

                                            yPre=(int)((DTYPE)pixelY/gridVoxelSpacing[1]);
                                            basis=(DTYPE)pixelY/gridVoxelSpacing[1]-(DTYPE)yPre;
                                            get_BSplineBasisValue<DTYPE>(basis, y-yPre,yBasis,yFirst);

                                            xPre=(int)((DTYPE)pixelX/gridVoxelSpacing[0]);
                                            basis=(DTYPE)pixelX/gridVoxelSpacing[0]-(DTYPE)xPre;
                                            get_BSplineBasisValue<DTYPE>(basis, x-xPre,xBasis,xFirst);

                                            basisValues[0]= xFirst * yBasis * zBasis ;
                                            basisValues[1]= xBasis * yFirst * zBasis ;
                                            basisValues[2]= xBasis * yBasis * zFirst ;

                                            correctFolding=true;
                                            addJacobianGradientValues<DTYPE>(jacobianMatrix,
                                                                             1.0,
                                                                             basisValues[0],
                                                                             basisValues[1],
                                                                             basisValues[2],
                                                                             foldingCorrection);
                                        } // detJac<0.0
                                    } // if x
                                }// x
                            }// if y
                        }// y
                    }// if z
                } // z
                // (Marc) I removed the normalisation by the voxel number as each gradient has to be normalised in the same way (NMI, BE, JAC)
                if(correctFolding){
                    gradient[0] = desorient.m[0][0]*foldingCorrection[0]
                            + desorient.m[0][1]*foldingCorrection[1]
                            + desorient.m[0][2]*foldingCorrection[2];
                    gradient[1] = desorient.m[1][0]*foldingCorrection[0]
                            + desorient.m[1][1]*foldingCorrection[1]
                            + desorient.m[1][2]*foldingCorrection[2];
                    gradient[2] = desorient.m[2][0]*foldingCorrection[0]
                            + desorient.m[2][1]*foldingCorrection[1]
                            + desorient.m[2][2]*foldingCorrection[2];
                    norm = (DTYPE)(5.0 * sqrt(gradient[0]*gradient[0] +
                                              gradient[1]*gradient[1] +
                                              gradient[2]*gradient[2]));

                    if(norm>0.0){
                        id = (z*splineControlPoint->ny+y)*splineControlPoint->nx+x;
                        controlPointPtrX[id] += (DTYPE)(realSpacing[0]*gradient[0]/norm);
                        controlPointPtrY[id] += (DTYPE)(realSpacing[1]*gradient[1]/norm);
                        controlPointPtrZ[id] += (DTYPE)(realSpacing[2]*gradient[2]/norm);
                    }
                }
            }
        }
    }
    free(jacobianDeterminant);
    free(jacobianMatrices);
    return std::numeric_limits<double>::quiet_NaN();
}
/* *************************************************************** */
template<class DTYPE>
double reg_spline_correctFoldingApprox_3D(nifti_image *splineControlPoint)
{

    unsigned int jacobianNumber = splineControlPoint->nx * splineControlPoint->ny * splineControlPoint->nz;

    mat33 *jacobianMatrices=(mat33 *)malloc(jacobianNumber * sizeof(mat33));
    DTYPE *jacobianDeterminant=(DTYPE *)malloc(jacobianNumber * sizeof(DTYPE));

    reg_spline_computeApproximateJacobianMatrices_3D<DTYPE>(splineControlPoint,
                                                            jacobianMatrices,
                                                            jacobianDeterminant);

    // The current Penalty term value is computed
    int jacIndex, i, j, k;
    double penaltyTerm=0, logDet;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, jacobianDeterminant) \
    private(jacIndex, i, j, k, logDet) \
    reduction(+:penaltyTerm)
#endif
    for(k=1; k<splineControlPoint->nz-1; k++){
        for(j=1; j<splineControlPoint->ny-1; j++){
            jacIndex = (k*splineControlPoint->ny+j)*splineControlPoint->nx+1;
            for(i=1; i<splineControlPoint->nx-1; i++){
                logDet = log(jacobianDeterminant[jacIndex++]);
#ifdef _USE_SQUARE_LOG_JAC
                penaltyTerm += logDet*logDet;
#else
                penaltyTerm +=  fabs(log(logDet));
#endif
            }
        }
    }
    if(penaltyTerm==penaltyTerm){
        free(jacobianDeterminant);
        free(jacobianMatrices);
        jacobianNumber = (splineControlPoint->nx-2) * (splineControlPoint->ny-2) * (splineControlPoint->nz-2);
        return penaltyTerm/(double)jacobianNumber;
    }

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, desorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);
    desorient=nifti_mat33_inverse(reorient);

    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = &controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];
    DTYPE *controlPointPtrZ = &controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz];

    DTYPE basisValues[3], foldingCorrection[3], gradient[3], norm;
    DTYPE xBasis=0, yBasis=0, zBasis=0, xFirst=0, yFirst=0, zFirst=0;
    int x, y, z, id, pixelX, pixelY, pixelZ;
    bool correctFolding;
    double detJac;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(splineControlPoint, jacobianDeterminant, jacobianMatrices, \
    controlPointPtrX, controlPointPtrY, controlPointPtrZ, desorient, realSpacing) \
    private(x, y, z, pixelX, pixelY, pixelZ, foldingCorrection, \
    xBasis, yBasis, zBasis, xFirst, yFirst, zFirst, jacIndex, detJac, \
    jacobianMatrix, basisValues, norm, correctFolding, id, gradient)
#endif
    for(z=0;z<splineControlPoint->nz;z++){
        for(y=0;y<splineControlPoint->ny;y++){
            for(x=0;x<splineControlPoint->nx;x++){

                foldingCorrection[0]=foldingCorrection[1]=foldingCorrection[2]=0;
                correctFolding=false;

                // Loop over all the control points in the surrounding area
                for(pixelZ=(int)((z-1));pixelZ<(int)((z+2)); pixelZ++){
                    if(pixelZ>0 && pixelZ<splineControlPoint->nz-1){

                        for(pixelY=(int)((y-1));pixelY<(int)((y+2)); pixelY++){
                            if(pixelY>0 && pixelY<splineControlPoint->ny-1){

                                for(pixelX=(int)((x-1));pixelX<(int)((x+2)); pixelX++){
                                    if(pixelX>0 && pixelX<splineControlPoint->nx-1){

                                        jacIndex = (pixelZ*splineControlPoint->ny+pixelY)*splineControlPoint->nx+pixelX;
                                        detJac = jacobianDeterminant[jacIndex];

                                        if(detJac<=0.0){

                                            get_BSplineBasisValue<DTYPE>(0, z-pixelZ+1, zBasis, zFirst);
                                            get_BSplineBasisValue<DTYPE>(0, y-pixelY+1, yBasis, yFirst);
                                            get_BSplineBasisValue<DTYPE>(0, x-pixelX+1, xBasis, xFirst);

                                            basisValues[0] = xFirst * yBasis * zBasis ;
                                            basisValues[1] = xBasis * yFirst * zBasis ;
                                            basisValues[2] = xBasis * yBasis * zFirst ;

                                            jacobianMatrix = jacobianMatrices[jacIndex];

                                            correctFolding=true;
                                            addJacobianGradientValues<DTYPE>(jacobianMatrix,
                                                                             1.0,
                                                                             basisValues[0],
                                                                             basisValues[1],
                                                                             basisValues[2],
                                                                             foldingCorrection);
                                        } // detJac<0.0
                                    } // if x
                                }// x
                            }// if y
                        }// y
                    }// if z
                } // z
                if(correctFolding){
                    gradient[0] = desorient.m[0][0]*foldingCorrection[0]
                            + desorient.m[0][1]*foldingCorrection[1]
                            + desorient.m[0][2]*foldingCorrection[2];
                    gradient[1] = desorient.m[1][0]*foldingCorrection[0]
                            + desorient.m[1][1]*foldingCorrection[1]
                            + desorient.m[1][2]*foldingCorrection[2];
                    gradient[2] = desorient.m[2][0]*foldingCorrection[0]
                            + desorient.m[2][1]*foldingCorrection[1]
                            + desorient.m[2][2]*foldingCorrection[2];
                    norm = (DTYPE)(5.0 * sqrt(gradient[0]*gradient[0]
                                              + gradient[1]*gradient[1]
                                              + gradient[2]*gradient[2]));

                    if(norm>(DTYPE)0.0){
                        id = (z*splineControlPoint->ny+y)*splineControlPoint->nx+x;
                        controlPointPtrX[id] += (DTYPE)(realSpacing[0]*gradient[0]/norm);
                        controlPointPtrY[id] += (DTYPE)(realSpacing[1]*gradient[1]/norm);
                        controlPointPtrZ[id] += (DTYPE)(realSpacing[2]*gradient[2]/norm);
                    }
                }
            }
        }
    }
    free(jacobianDeterminant);
    free(jacobianMatrices);
    return std::numeric_limits<double>::quiet_NaN();
}
/* *************************************************************** */
extern "C++"
double reg_spline_correctFolding(nifti_image *splineControlPoint,
                                 nifti_image *referenceImage,
                                 bool approx)
{

    if(splineControlPoint->nz==1){
        if(approx){
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                return reg_spline_correctFoldingApprox_2D<float>
                        (splineControlPoint);
                break;
            case NIFTI_TYPE_FLOAT64:
                return reg_spline_correctFoldingApprox_2D<double>
                        (splineControlPoint);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The bending energy gradient has not computed\n");
                exit(1);
            }
        }
        else{
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                return reg_spline_correctFolding_2D<float>
                        (splineControlPoint, referenceImage);
                break;
            case NIFTI_TYPE_FLOAT64:
                return reg_spline_correctFolding_2D<double>
                        (splineControlPoint, referenceImage);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The bending energy gradient has not computed\n");
                exit(1);
            }
        }
    }
    else{
        if(approx){
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                return reg_spline_correctFoldingApprox_3D<float>
                        (splineControlPoint);
                break;
            case NIFTI_TYPE_FLOAT64:
                return reg_spline_correctFoldingApprox_3D<double>
                        (splineControlPoint);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The bending energy gradient has not computed\n");
                exit(1);
            }
        }
        else{
            switch(splineControlPoint->datatype){
            case NIFTI_TYPE_FLOAT32:
                return reg_spline_correctFolding_3D<float>
                        (splineControlPoint, referenceImage);
                break;
            case NIFTI_TYPE_FLOAT64:
                return reg_spline_correctFolding_3D<double>
                        (splineControlPoint, referenceImage);
                break;
            default:
                fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the Jacobian determinant gradient\n");
                fprintf(stderr,"[NiftyReg ERROR] The bending energy gradient has not computed\n");
                exit(1);
            }
        }
    }
}
/* *************************************************************** */
/* *************************************************************** */
template <class DTYPE>
void reg_spline_GetJacobianMap2D(nifti_image *splineControlPoint,
                                 nifti_image *jacobianImage)
{
    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = static_cast<DTYPE *>(&controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);

    DTYPE *jacobianMapPtr = static_cast<DTYPE *>(jacobianImage->data);

    DTYPE yBasis[4],yFirst[4],temp[4],first[4];
    DTYPE basisX[16], basisY[16];
    DTYPE basis, oldBasis=(DTYPE)(1.1);

    DTYPE xControlPointCoordinates[16];
    DTYPE yControlPointCoordinates[16];

    DTYPE gridVoxelSpacing[2];
    gridVoxelSpacing[0] = splineControlPoint->dx / jacobianImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / jacobianImage->dy;

    unsigned int coord=0;

    DTYPE realSpacing[2];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
    }

    /* In case the matrix is not diagonal, the jacobian has to be reoriented */
    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    for(int y=0; y<jacobianImage->ny; y++){

        int yPre=(int)((DTYPE)y/gridVoxelSpacing[1]);
        basis=(DTYPE)y/gridVoxelSpacing[1]-(DTYPE)yPre;
        if(basis<0.0) basis=0.0; //rounding error
        get_BSplineBasisValues<DTYPE>(basis, yBasis, yFirst);

        for(int x=0; x<jacobianImage->nx; x++){

            int xPre=(int)((DTYPE)x/gridVoxelSpacing[0]);
            basis=(DTYPE)x/gridVoxelSpacing[0]-(DTYPE)xPre;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, temp, first);

            coord=0;
            for(int b=0; b<4; b++){
                for(int a=0; a<4; a++){
                    basisX[coord]=yBasis[b]*first[a];   // y * x'
                    basisY[coord]=yFirst[b]*temp[a];    // y'* x
                    coord++;
                }
            }

            if(basis<=oldBasis || x==0){
                get_GridValues<DTYPE>(xPre,
                                      yPre,
                                      splineControlPoint,
                                      controlPointPtrX,
                                      controlPointPtrY,
                                      xControlPointCoordinates,
                                      yControlPointCoordinates,
                                      false, // no approx
                                      false // not disp
                                      );
            }
            oldBasis=basis;
            DTYPE Tx_x=0.0;
            DTYPE Ty_x=0.0;
            DTYPE Tx_y=0.0;
            DTYPE Ty_y=0.0;

            for(int a=0; a<16; a++){
                Tx_x += basisX[a]*xControlPointCoordinates[a];
                Ty_x += basisX[a]*yControlPointCoordinates[a];

                Tx_y += basisY[a]*xControlPointCoordinates[a];
                Ty_y += basisY[a]*yControlPointCoordinates[a];
            }

            memset(&jacobianMatrix, 0, sizeof(mat33));
            jacobianMatrix.m[2][2]=1.0f;
            jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
            jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
            jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
            jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);

            jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);

            *jacobianMapPtr++ = nifti_mat33_determ(jacobianMatrix);
        }
    }
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_GetJacobianMap3D(nifti_image *splineControlPoint,
                                 nifti_image *jacobianImage)
{
    DTYPE *controlPointPtrX = static_cast<DTYPE *>(splineControlPoint->data);
    DTYPE *controlPointPtrY = static_cast<DTYPE *>(&controlPointPtrX[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);
    DTYPE *controlPointPtrZ = static_cast<DTYPE *>(&controlPointPtrY[splineControlPoint->nx*splineControlPoint->ny*splineControlPoint->nz]);

    DTYPE *jacobianMapPtr = static_cast<DTYPE *>(jacobianImage->data);

    DTYPE zBasis[4],zFirst[4],temp[4],first[4];
    DTYPE tempX[16], tempY[16], tempZ[16];
    DTYPE basisX[64], basisY[64], basisZ[64];
    DTYPE basis, oldBasis=(DTYPE)(1.1);

    DTYPE xControlPointCoordinates[64];
    DTYPE yControlPointCoordinates[64];
    DTYPE zControlPointCoordinates[64];

    DTYPE gridVoxelSpacing[3];
    gridVoxelSpacing[0] = splineControlPoint->dx / jacobianImage->dx;
    gridVoxelSpacing[1] = splineControlPoint->dy / jacobianImage->dy;
    gridVoxelSpacing[2] = splineControlPoint->dz / jacobianImage->dz;
    unsigned int coord=0;

    DTYPE realSpacing[3];
    if(splineControlPoint->sform_code>0){
        reg_getRealImageSpacing(splineControlPoint,realSpacing);
    }
    else{
        realSpacing[0]=splineControlPoint->dx;
        realSpacing[1]=splineControlPoint->dy;
        realSpacing[2]=splineControlPoint->dz;
    }

    mat33 reorient, jacobianMatrix;
    reg_getReorientationMatrix(splineControlPoint, &reorient);

    for(int z=0; z<jacobianImage->nz; z++){

        int zPre=(int)((DTYPE)z/gridVoxelSpacing[2]);
        basis=(DTYPE)z/gridVoxelSpacing[2]-(DTYPE)zPre;
        if(basis<0.0) basis=0.0; //rounding error
        get_BSplineBasisValues<DTYPE>(basis, zBasis, zFirst);

        for(int y=0; y<jacobianImage->ny; y++){

            int yPre=(int)((DTYPE)y/gridVoxelSpacing[1]);
            basis=(DTYPE)y/gridVoxelSpacing[1]-(DTYPE)yPre;
            if(basis<0.0) basis=0.0; //rounding error
            get_BSplineBasisValues<DTYPE>(basis, temp, first);

            coord=0;
            for(int c=0; c<4; c++){
                for(int b=0; b<4; b++){
                    tempX[coord]=zBasis[c]*temp[b]; // z * y
                    tempY[coord]=zBasis[c]*first[b];// z * y'
                    tempZ[coord]=zFirst[c]*temp[b]; // z'* y
                    coord++;
                }
            }

            for(int x=0; x<jacobianImage->nx; x++){

                int xPre=(int)((DTYPE)x/gridVoxelSpacing[0]);
                basis=(DTYPE)x/gridVoxelSpacing[0]-(DTYPE)xPre;
                if(basis<0.0) basis=0.0; //rounding error
                get_BSplineBasisValues<DTYPE>(basis, temp, first);

                coord=0;
                for(int bc=0; bc<16; bc++){
                    for(int a=0; a<4; a++){
                        basisX[coord]=tempX[bc]*first[a];   // z * y * x'
                        basisY[coord]=tempY[bc]*temp[a];    // z * y'* x
                        basisZ[coord]=tempZ[bc]*temp[a];    // z'* y * x
                        coord++;
                    }
                }

                if(basis<=oldBasis || x==0){
                    get_GridValues<DTYPE>(xPre,
                                          yPre,
                                          zPre,
                                          splineControlPoint,
                                          controlPointPtrX,
                                          controlPointPtrY,
                                          controlPointPtrZ,
                                          xControlPointCoordinates,
                                          yControlPointCoordinates,
                                          zControlPointCoordinates,
                                          false, // no approx
                                          false // not disp
                                          );
                }
                oldBasis=basis;

                DTYPE Tx_x=0.0;
                DTYPE Ty_x=0.0;
                DTYPE Tz_x=0.0;
                DTYPE Tx_y=0.0;
                DTYPE Ty_y=0.0;
                DTYPE Tz_y=0.0;
                DTYPE Tx_z=0.0;
                DTYPE Ty_z=0.0;
                DTYPE Tz_z=0.0;

                for(int a=0; a<64; a++){
                    Tx_x += basisX[a]*xControlPointCoordinates[a];
                    Tx_y += basisY[a]*xControlPointCoordinates[a];
                    Tx_z += basisZ[a]*xControlPointCoordinates[a];

                    Ty_x += basisX[a]*yControlPointCoordinates[a];
                    Ty_y += basisY[a]*yControlPointCoordinates[a];
                    Ty_z += basisZ[a]*yControlPointCoordinates[a];

                    Tz_x += basisX[a]*zControlPointCoordinates[a];
                    Tz_y += basisY[a]*zControlPointCoordinates[a];
                    Tz_z += basisZ[a]*zControlPointCoordinates[a];
                }

                jacobianMatrix.m[0][0]= (float)(Tx_x / realSpacing[0]);
                jacobianMatrix.m[0][1]= (float)(Tx_y / realSpacing[1]);
                jacobianMatrix.m[0][2]= (float)(Tx_z / realSpacing[2]);
                jacobianMatrix.m[1][0]= (float)(Ty_x / realSpacing[0]);
                jacobianMatrix.m[1][1]= (float)(Ty_y / realSpacing[1]);
                jacobianMatrix.m[1][2]= (float)(Ty_z / realSpacing[2]);
                jacobianMatrix.m[2][0]= (float)(Tz_x / realSpacing[0]);
                jacobianMatrix.m[2][1]= (float)(Tz_y / realSpacing[1]);
                jacobianMatrix.m[2][2]= (float)(Tz_z / realSpacing[2]);

                jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
                DTYPE detJac = nifti_mat33_determ(jacobianMatrix);

                *jacobianMapPtr++ = detJac;
            }
        }
    }
}
/* *************************************************************** */
void reg_spline_GetJacobianMap(nifti_image *splineControlPoint,
                               nifti_image *jacobianImage)
{
    if(splineControlPoint->nz==1){
        switch(jacobianImage->datatype){
        case NIFTI_TYPE_FLOAT32:
            reg_spline_GetJacobianMap2D<float>(splineControlPoint, jacobianImage);
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_spline_GetJacobianMap2D<double>(splineControlPoint, jacobianImage);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the jacobian map image\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian map has not computed\n");
            exit(1);
        }
    }else{
        switch(jacobianImage->datatype){
        case NIFTI_TYPE_FLOAT32:
            reg_spline_GetJacobianMap3D<float>(splineControlPoint, jacobianImage);
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_spline_GetJacobianMap3D<double>(splineControlPoint, jacobianImage);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the jacobian map image\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian map has not computed\n");
            exit(1);
        }
    }
}
/* *************************************************************** */
/* *************************************************************** */
void reg_spline_GetJacobianMatrix(nifti_image *referenceImage,
                                  nifti_image *splineControlPoint,
                                  mat33 *jacobianMatrices)
{
    if(splineControlPoint->nz>1){
        switch(splineControlPoint->datatype){
        case NIFTI_TYPE_FLOAT32:
            reg_spline_computeJacobianMatrices_3D<float>(referenceImage,splineControlPoint,jacobianMatrices, NULL);
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_spline_computeJacobianMatrices_3D<double>(referenceImage,splineControlPoint,jacobianMatrices, NULL);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the control point image\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian matrix image has not been computed\n");
            exit(1);
        }
    }
    else{
        switch(splineControlPoint->datatype){
        case NIFTI_TYPE_FLOAT32:
            reg_spline_computeJacobianMatrices_2D<float>(referenceImage,splineControlPoint,jacobianMatrices, NULL);
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_spline_computeJacobianMatrices_2D<double>(referenceImage,splineControlPoint,jacobianMatrices, NULL);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the control point image\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian matrix image has not been computed\n");
            exit(1);
        }
    }
}
/* *************************************************************** */
/* *************************************************************** */
void reg_spline_GetJacobianMatrixFull(nifti_image *referenceImage,
                                      nifti_image *splineControlPoint,
                                      mat33 *jacobianMatrices)
{
    if(splineControlPoint->nz>1){
        switch(splineControlPoint->datatype){
        case NIFTI_TYPE_FLOAT32:
            reg_spline_computeJacobianMatricesFull_3D<float>(referenceImage,splineControlPoint,jacobianMatrices);
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_spline_computeJacobianMatricesFull_3D<double>(referenceImage,splineControlPoint,jacobianMatrices);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the control point image\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian matrix image has not been computed\n");
            exit(1);
        }
    }
    else{
        switch(splineControlPoint->datatype){
        case NIFTI_TYPE_FLOAT32:
            reg_spline_computeJacobianMatricesFull_2D<float>(referenceImage,splineControlPoint,jacobianMatrices);
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_spline_computeJacobianMatricesFull_2D<double>(referenceImage,splineControlPoint,jacobianMatrices);
            break;
        default:
            fprintf(stderr,"[NiftyReg ERROR] Only single or double precision is implemented for the control point image\n");
            fprintf(stderr,"[NiftyReg ERROR] The jacobian matrix image has not been computed\n");
            exit(1);
        }
    }
}
/* *************************************************************** */
/* *************************************************************** */
template <class DTYPE>
void reg_defField_getJacobianMap2D(nifti_image *deformationField,
                                   nifti_image *jacobianDeterminant,
                                   mat33 *jacobianMatrices)
{
    size_t voxelNumber=deformationField->nx*deformationField->ny;

    DTYPE *jacDetPtr=NULL;
    if(jacobianDeterminant!=NULL)
        jacDetPtr=static_cast<DTYPE *>(jacobianDeterminant->data);

    DTYPE realSpacing[2];
    if(deformationField->sform_code>0){
        reg_getRealImageSpacing(deformationField,realSpacing);
    }
    else{
        realSpacing[0]=deformationField->dx;
        realSpacing[1]=deformationField->dy;
    }

    mat33 jacobianMatrix, reorient;
    reg_getReorientationMatrix(deformationField,&reorient);

    DTYPE *deformationPtrX = static_cast<DTYPE *>(deformationField->data);
    DTYPE *deformationPtrY = &deformationPtrX[voxelNumber];

    DTYPE basis[2]={1.0,0.0};
    DTYPE first[2]={-1.0,1.0};
    DTYPE firstX, firstY, defX, defY;

    int currentIndex, x, y, a, b, index;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(deformationField, jacobianDeterminant, jacobianMatrices, reorient, \
    basis, first, jacDetPtr, deformationPtrX, deformationPtrY, realSpacing) \
    private(currentIndex, x, y, a, b, index, \
    jacobianMatrix, defX, defY, firstX, firstY)
#endif
    for(y=0;y<deformationField->ny-1;++y){
        currentIndex=y*deformationField->nx;
        for(x=0;x<deformationField->nx-1;++x){

            memset(&jacobianMatrix,0,sizeof(mat33));

            for(b=0;b<2;++b){
                index=(y+b)*deformationField->nx+x;
                for(a=0;a<2;++a){

                    // Compute the basis function values
                    firstX=first[a]*basis[b];
                    firstY=basis[a]*first[b];

                    // Get the deformation field values
                    defX = deformationPtrX[index];
                    defY = deformationPtrY[index];

                    // Symmetric difference to compute the derivatives
                    jacobianMatrix.m[0][0] += firstX*defX;
                    jacobianMatrix.m[0][1] += firstY*defX;
                    jacobianMatrix.m[1][0] += firstX*defY;
                    jacobianMatrix.m[1][1] += firstY*defY;

                    ++index;
                }//a
            }//b

            // reorient and scale the Jacobian matrix
            jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
            jacobianMatrix.m[0][0] /= realSpacing[0];
            jacobianMatrix.m[0][1] /= realSpacing[1];
            jacobianMatrix.m[0][2] =0.;
            jacobianMatrix.m[1][0] /= realSpacing[0];
            jacobianMatrix.m[1][1] /= realSpacing[1];
            jacobianMatrix.m[1][2] =0.;
            jacobianMatrix.m[2][0] =0.;
            jacobianMatrix.m[2][1] =0.;
            jacobianMatrix.m[2][2] =1.;

            // Update the output arrays if required
            if(jacobianDeterminant!=NULL)
                jacDetPtr[currentIndex] = nifti_mat33_determ(jacobianMatrix);
            if(jacobianMatrices!=NULL)
                jacobianMatrices[currentIndex]=jacobianMatrix;
            // Increment the pointer
            currentIndex++;
        }// x jacImage
    }//y jacImage
    // Sliding is assumed. The Jacobian at the boundary are then replicated
    for(y=0;y<deformationField->ny;++y){
        currentIndex=y*deformationField->nx;
        for(x=0;x<deformationField->nx;++x){
            index=currentIndex;
            if(x==deformationField->nx-1) index -= 1;
            if(y==deformationField->ny-1) index -= deformationField->nx;
            if(currentIndex!=index){
                if(jacobianDeterminant!=NULL)
                    jacDetPtr[currentIndex] = jacDetPtr[index];
                if(jacobianMatrices!=NULL)
                    jacobianMatrices[currentIndex] = jacobianMatrices[index];
            }
            ++currentIndex;
        } // x
    } // y
}
/* *************************************************************** */
template <class DTYPE>
void reg_defField_getJacobianMap3D(nifti_image *deformationField,
                                   nifti_image *jacobianDeterminant,
                                   mat33 *jacobianMatrices)
{
    size_t voxelNumber=deformationField->nx*deformationField->ny*deformationField->nz;

    DTYPE *jacDetPtr=NULL;
    if(jacobianDeterminant!=NULL)
        jacDetPtr=static_cast<DTYPE *>(jacobianDeterminant->data);

    DTYPE realSpacing[3];
    if(deformationField->sform_code>0){
        reg_getRealImageSpacing(deformationField,realSpacing);
    }
    else{
        realSpacing[0]=deformationField->dx;
        realSpacing[1]=deformationField->dy;
        realSpacing[2]=deformationField->dz;
    }

    mat33 jacobianMatrix;
    mat33 reorient=nifti_mat33_inverse(nifti_mat33_polar(reg_mat44_to_mat33(&deformationField->sto_xyz)));

    DTYPE *deformationPtrX = static_cast<DTYPE *>(deformationField->data);
    DTYPE *deformationPtrY = &deformationPtrX[voxelNumber];
    DTYPE *deformationPtrZ = &deformationPtrY[voxelNumber];

    DTYPE basis[2]={1.0,0.0};
    DTYPE first[2]={-1.0,1.0};
    DTYPE firstX, firstY, firstZ, defX, defY, defZ;

    int currentIndex, x, y, z, a, b, c, currentZ, index;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    shared(deformationField, jacobianDeterminant, jacobianMatrices, reorient, \
    basis, first, jacDetPtr, deformationPtrX, deformationPtrY, deformationPtrZ, realSpacing) \
    private(currentIndex, x, y, z, a, b, c, currentZ, index, \
    jacobianMatrix, defX, defY, defZ, firstX, firstY, firstZ)
#endif
    for(z=0;z<deformationField->nz-1;++z){
        for(y=0;y<deformationField->ny-1;++y){
            currentIndex=(z*deformationField->ny+y)*deformationField->nx;
            for(x=0;x<deformationField->nx-1;++x){

                memset(&jacobianMatrix,0,sizeof(mat33));

                for(c=0;c<2;++c){
                    currentZ=z+c;
                    for(b=0;b<2;++b){
                        index=(currentZ*deformationField->ny+y+b)*deformationField->nx+x;
                        for(a=0;a<2;++a){

                            // Compute the basis function values
                            firstX=first[a]*basis[b]*basis[c];
                            firstY=basis[a]*first[b]*basis[c];
                            firstZ=basis[a]*basis[b]*first[c];

                            // Get the deformation field values
                            defX = deformationPtrX[index];
                            defY = deformationPtrY[index];
                            defZ = deformationPtrZ[index];

                            // Symmetric difference to compute the derivatives
                            jacobianMatrix.m[0][0] += firstX*defX;
                            jacobianMatrix.m[0][1] += firstY*defX;
                            jacobianMatrix.m[0][2] += firstZ*defX;
                            jacobianMatrix.m[1][0] += firstX*defY;
                            jacobianMatrix.m[1][1] += firstY*defY;
                            jacobianMatrix.m[1][2] += firstZ*defY;
                            jacobianMatrix.m[2][0] += firstX*defZ;
                            jacobianMatrix.m[2][1] += firstY*defZ;
                            jacobianMatrix.m[2][2] += firstZ*defZ;

                            ++index;
                        }//a
                    }//b
                }//c

                // reorient and scale the Jacobian matrix
                jacobianMatrix=nifti_mat33_mul(reorient,jacobianMatrix);
                jacobianMatrix.m[0][0] /= realSpacing[0];
                jacobianMatrix.m[0][1] /= realSpacing[1];
                jacobianMatrix.m[0][2] /= realSpacing[2];
                jacobianMatrix.m[1][0] /= realSpacing[0];
                jacobianMatrix.m[1][1] /= realSpacing[1];
                jacobianMatrix.m[1][2] /= realSpacing[2];
                jacobianMatrix.m[2][0] /= realSpacing[0];
                jacobianMatrix.m[2][1] /= realSpacing[1];
                jacobianMatrix.m[2][2] /= realSpacing[2];

                // Update the output arrays if required
                if(jacobianDeterminant!=NULL)
                    jacDetPtr[currentIndex] = nifti_mat33_determ(jacobianMatrix);
                if(jacobianMatrices!=NULL)
                    jacobianMatrices[currentIndex]=jacobianMatrix;
                // Increment the pointer
                currentIndex++;
            }// x jacImage
        }//y jacImage
    }//z jacImage
    // Sliding is assumed. The Jacobian at the boundary are then replicated
    for(z=0;z<deformationField->nz;++z){
        currentIndex=z*deformationField->nx*deformationField->ny;
        for(y=0;y<deformationField->ny;++y){
            for(x=0;x<deformationField->nx;++x){
                index=currentIndex;
                if(x==deformationField->nx-1) index -= 1;
                if(y==deformationField->ny-1) index -= deformationField->nx;
                if(z==deformationField->nz-1) index -= deformationField->nx*deformationField->ny;
                if(currentIndex!=index){
                    if(jacobianDeterminant!=NULL)
                        jacDetPtr[currentIndex] = jacDetPtr[index];
                    if(jacobianMatrices!=NULL)
                        jacobianMatrices[currentIndex] = jacobianMatrices[index];
                }
                ++currentIndex;
            } // x
        } // y
    } // z
}
/* *************************************************************** */
void reg_defField_getJacobianMap(nifti_image *deformationField,
                                 nifti_image *jacobianImage)
{
    if(deformationField->datatype!=jacobianImage->datatype){
        printf("[NiftyReg ERROR] reg_defField_getJacobianMap\n");
        printf("[NiftyReg ERROR] Both input images have different type. Exit\n");
        exit(1);
    }
    switch(deformationField->datatype){
    case NIFTI_TYPE_FLOAT32:
        if(deformationField->nz>1)
            reg_defField_getJacobianMap3D<float>(deformationField,jacobianImage,NULL);
        else reg_defField_getJacobianMap2D<float>(deformationField,jacobianImage,NULL);
        break;
    case NIFTI_TYPE_FLOAT64:
        if(deformationField->nz>1)
            reg_defField_getJacobianMap3D<double>(deformationField,jacobianImage,NULL);
        else reg_defField_getJacobianMap2D<double>(deformationField,jacobianImage,NULL);
        break;
    default:
        printf("[NiftyReg ERROR] reg_defField_getJacobianMap\n");
        printf("[NiftyReg ERROR] Voxel type unsupported.\n");
        exit(1);
    }
}
/* *************************************************************** */
/* *************************************************************** */
void reg_defField_getJacobianMatrix(nifti_image *deformationField,
                                    mat33 *jacobianMatrices)
{
    switch(deformationField->datatype){
    case NIFTI_TYPE_FLOAT32:
        if(deformationField->nz>1)
            reg_defField_getJacobianMap3D<float>(deformationField,NULL,jacobianMatrices);
        else reg_defField_getJacobianMap2D<float>(deformationField,NULL,jacobianMatrices);
        break;
    case NIFTI_TYPE_FLOAT64:
        if(deformationField->nz>1)
            reg_defField_getJacobianMap3D<double>(deformationField,NULL,jacobianMatrices);
        else reg_defField_getJacobianMap2D<double>(deformationField,NULL,jacobianMatrices);
        break;
    default:
        printf("[NiftyReg ERROR] reg_defField_getJacobianMap\n");
        printf("[NiftyReg ERROR] Voxel type unsupported.\n");
        exit(1);
    }
}
/* *************************************************************** */
template <class DTYPE>
void reg_spline_GetJacobianMatricesFromVelocityField1(nifti_image* referenceImage,
                                                      nifti_image* velocityFieldImage,
                                                      mat33* jacobianMatrices)
{
    if( velocityFieldImage->intent_code!=NIFTI_INTENT_VECTOR ||
            strcmp(velocityFieldImage->intent_name,"NREG_VEL_STEP")!=0 ){
        fprintf(stderr, "[NiftyReg ERROR] reg_spline_GetJacobianMatricesFromVelocityField_2D - the provide grid is not a velocity field\n");
        exit(1);
    }

    // A first deformation field is allocated based on the reference image dimension
    nifti_image *deformationFieldA = nifti_copy_nim_info(referenceImage);
    deformationFieldA->dim[0]=deformationFieldA->ndim=5;
    deformationFieldA->dim[1]=deformationFieldA->nx=referenceImage->nx;
    deformationFieldA->dim[2]=deformationFieldA->ny=referenceImage->ny;
    deformationFieldA->dim[3]=deformationFieldA->nz=referenceImage->nz;
    deformationFieldA->dim[4]=deformationFieldA->nt=1;
    deformationFieldA->pixdim[4]=deformationFieldA->dt=1.0;
    deformationFieldA->dim[5]=deformationFieldA->nu=velocityFieldImage->nu;
    deformationFieldA->pixdim[5]=deformationFieldA->du=1.0;
    deformationFieldA->dim[6]=deformationFieldA->nv=1;
    deformationFieldA->pixdim[6]=deformationFieldA->dv=1.0;
    deformationFieldA->dim[7]=deformationFieldA->nw=1;
    deformationFieldA->pixdim[7]=deformationFieldA->dw=1.0;
    deformationFieldA->nvox=deformationFieldA->nx *
            deformationFieldA->ny *
            deformationFieldA->nz *
            deformationFieldA->nt *
            deformationFieldA->nu;
    deformationFieldA->nbyper = velocityFieldImage->nbyper;
    deformationFieldA->datatype = velocityFieldImage->datatype;
    deformationFieldA->data = (void *)malloc(deformationFieldA->nvox * deformationFieldA->nbyper);
    // A second deformation field is allocated based on the first deformation field
    nifti_image *deformationFieldB = nifti_copy_nim_info(deformationFieldA);
    deformationFieldB->data = (void *)malloc(deformationFieldB->nvox * deformationFieldB->nbyper);

    // The initial deformation field is computed
    reg_spline_getDeformationField(velocityFieldImage,
                                   deformationFieldA,
                                   NULL, // mask
                                   false, //composition
                                   true // bspline
                                   );

    // The deformation field is converted from deformation field to displacement field
    reg_getDisplacementFromDeformation(deformationFieldA);
    // The deformation field is scaled
    float scalingValue = pow(2.0f,fabs(velocityFieldImage->intent_p1));
    if(velocityFieldImage->intent_p1<0)
        // backward deformation field is scaled down
        reg_tools_addSubMulDivValue(deformationFieldA,
                                    deformationFieldA,
                                    -scalingValue,
                                    3);
    else
        // forward deformation field is scaled down
        reg_tools_addSubMulDivValue(deformationFieldA,
                                    deformationFieldA,
                                    scalingValue,
                                    3);
    // The displacement field is converted back into a deformation field
    reg_getDeformationFromDisplacement(deformationFieldA);
    memcpy(deformationFieldB->data,
           deformationFieldA->data,
           deformationFieldA->nvox*deformationFieldA->nbyper);

    size_t voxelNumber=referenceImage->nx*referenceImage->ny*referenceImage->nz;

    // The Jacobian matrices values are initialised to identity
    for(size_t i=0;i<voxelNumber;++i)
        reg_mat33_eye(&jacobianMatrices[i]);

    // Create a temporary Jacobian matrix array
    mat33 *tempJacobianMatrix=(mat33 *)malloc(voxelNumber*sizeof(mat33));

    // Loop over the composition step. Note that there is no need to generate the
    // final deformation field to compute the Jacobian matrices
    for(size_t i=0;i<(size_t)fabs(velocityFieldImage->intent_p1)-1;++i){

        // The jacobian matrices are computed at every voxel
        // for the current deformation field
        reg_defField_getJacobianMatrix(deformationFieldA,
                                       tempJacobianMatrix);

        // The overall Jacobian matrices are updated
        for(size_t i=0;i<voxelNumber;++i)
            jacobianMatrices[i]=nifti_mat33_mul(tempJacobianMatrix[i],jacobianMatrices[i]);

        // The deformation field is updated
        reg_defField_compose(deformationFieldA,
                             deformationFieldB,
                             NULL);
        memcpy(deformationFieldA->data,deformationFieldB->data,
               deformationFieldA->nvox*deformationFieldA->nbyper);

    }//composition step

    nifti_image_free(deformationFieldA);
    nifti_image_free(deformationFieldB);
    free(tempJacobianMatrix);

}
/* *************************************************************** */
int reg_spline_GetJacobianMatricesFromVelocityField(nifti_image* referenceImage,
                                                    nifti_image* velocityFieldImage,
                                                    mat33* jacobianMatrices)
{
    switch(velocityFieldImage->datatype){
    case NIFTI_TYPE_FLOAT32:
        reg_spline_GetJacobianMatricesFromVelocityField1<float>
                (referenceImage,velocityFieldImage, jacobianMatrices);
        break;
    case NIFTI_TYPE_FLOAT64:
        reg_spline_GetJacobianMatricesFromVelocityField1<double>
                (referenceImage,velocityFieldImage, jacobianMatrices);
        break;
    default:
        fprintf(stderr,"[NiftyReg ERROR] reg_spline_GetJacobianMatricesFromVelocityField_2D\n");
        fprintf(stderr,"[NiftyReg ERROR] Only implemented for float or double precision\n");
        return 1;
        break;
    }
    return 0;
}
/* *************************************************************** */
/* *************************************************************** */
template<class DTYPE>
void reg_getDeterminantsFromMatrices(nifti_image *jacobianDetImage,
                                     mat33 *jacobianMatrices
                                     )
{
    size_t voxelNumber=jacobianDetImage->nx*jacobianDetImage->ny*jacobianDetImage->nz;
    DTYPE *jacDetPtr=static_cast<DTYPE *>(jacobianDetImage->data);
    for(size_t voxel=0;voxel<voxelNumber;++voxel)
        jacDetPtr[voxel]=nifti_mat33_determ(jacobianMatrices[voxel]);
}
/* *************************************************************** */
int reg_spline_GetJacobianDetFromVelocityField(nifti_image* jacobianDetImage,
                                               nifti_image* velocityFieldImage
                                               )
{
    // create an array of mat33
    size_t voxelNumber=jacobianDetImage->nx*jacobianDetImage->ny*jacobianDetImage->nz;
    mat33 *jacobianMatrices=(mat33 *)malloc(voxelNumber*sizeof(mat33));

    // Compute all Jacobian matrices
    reg_spline_GetJacobianMatricesFromVelocityField(jacobianDetImage,
                                                    velocityFieldImage,
                                                    jacobianMatrices);
    // Compute and store all determinant
    switch(jacobianDetImage->datatype){
    case NIFTI_TYPE_FLOAT32:
        reg_getDeterminantsFromMatrices<float>
                (jacobianDetImage,jacobianMatrices);
        break;
    case NIFTI_TYPE_FLOAT64:
        reg_getDeterminantsFromMatrices<double>
                (jacobianDetImage,jacobianMatrices);
        break;
    default:
        fprintf(stderr,"[NiftyReg ERROR] reg_getDeterminantsFromMatrices\n");
        fprintf(stderr,"[NiftyReg ERROR] Only implemented for float or double precision\n");
        return 1;
        break;
    }
    free(jacobianMatrices);
    return 0;
}

/* *************************************************************** */
/* *************************************************************** */
