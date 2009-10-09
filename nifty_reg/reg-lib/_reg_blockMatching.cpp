/*
 *  _reg_blockMatching.cpp
 *  
 *
 *  Created by Marc Modat and Pankaj Daga on 24/03/2009.
 *  Copyright (c) 2009, University College London. All rights reserved.
 *  Centre for Medical Image Computing (CMIC)
 *  See the LICENSE.txt file in the nifty_reg root folder
 *
 */

#include "_reg_blockMatching.h"
#include "_reg_affineTransformation.h"
#include <queue>
#include <iostream>

// Helper function: Get the square of the Euclidean distance
double get_square_distance(float * first_point3D, float * second_point3D)
{
	return 	(first_point3D[0]-second_point3D[0])*(first_point3D[0]-second_point3D[0]) +
		(first_point3D[1]-second_point3D[1])*(first_point3D[1]-second_point3D[1]) +
		(first_point3D[2]-second_point3D[2])*(first_point3D[2]-second_point3D[2]);
}

// Heap sort
void reg_heapSort(float *array_tmp, int *index_tmp, int blockNum)
{
	float *array = &array_tmp[-1];
	int *index = &index_tmp[-1];
	int l=(blockNum >> 1)+1;
	int ir=blockNum;
	float val;
	int iVal;
	for(;;){
		if(l>1){
			val=array[--l];
			iVal=index[l];
		}
		else{
			val=array[ir];
			iVal=index[ir];
			array[ir]=array[1];
			index[ir]=index[1];
			if(--ir == 1){
				array[1]=val;
				index[1]=iVal;
				break;
			}
		}
		int i=l;
		int j=l+l;
		while(j<=ir){
			if(j<ir && array[j]<array[j+1]) j++;
			if(val<array[j]){
				array[i]=array[j];
				index[i]=index[j];
				i=j;
				j<<=1;
			}
			else break;
		}
		array[i]=val;
		index[i]=iVal;
	}
}

template <class DTYPE>
void _reg_set_active_blocks(nifti_image *targetImage, _reg_blockMatchingParam *params, int *mask)
{
	const int totalBlockNumber = params->blockNumber[0]*params->blockNumber[1]*params->blockNumber[2];
	float *varianceArray=(float *)malloc(totalBlockNumber*sizeof(float));
    int *indexArray=(int *)malloc(totalBlockNumber*sizeof(int));

    int *maskPtr=&mask[0];

	int unusableBlock=0;

	DTYPE *targetPtr = static_cast<DTYPE *>(targetImage->data);
	int blockIndex=0;	
	for(int k=0; k<params->blockNumber[2]; k++){
		for(int j=0; j<params->blockNumber[1]; j++){
			for(int i=0; i<params->blockNumber[0]; i++){

				float mean=0.0f;
				float voxelNumber=0.0f;
				for(int z=k*BLOCK_WIDTH; z<(k+1)*BLOCK_WIDTH; z++){
					if(z<targetImage->nz){
                        DTYPE *targetPtrZ=&targetPtr[z*targetImage->nx*targetImage->ny];
                        int *maskPtrZ=&maskPtr[z*targetImage->nx*targetImage->ny];
						for(int y=j*BLOCK_WIDTH; y<(j+1)*BLOCK_WIDTH; y++){
							if(y<targetImage->ny){
                                DTYPE *targetPtrXYZ=&targetPtrZ[y*targetImage->nx+i*BLOCK_WIDTH];
                                int *maskPtrXYZ=&maskPtrZ[y*targetImage->nx+i*BLOCK_WIDTH];
								for(int x=i*BLOCK_WIDTH; x<(i+1)*BLOCK_WIDTH; x++){
									if(x<targetImage->nx && *maskPtrXYZ>-1){
										DTYPE value = *targetPtrXYZ;
										if(value!=0.0){
											mean += (float)value;
											voxelNumber++;
										}
									}
									targetPtrXYZ++;
									
								}
							}
						}
					}
				}
				if(voxelNumber>BLOCK_SIZE/2){
					mean /= voxelNumber;
					float variance=0.0f;
					for(int z=k*BLOCK_WIDTH; z<(k+1)*BLOCK_WIDTH; z++){
						if(z<targetImage->nz){
							DTYPE *targetPtrZ=&targetPtr[z*targetImage->nx*targetImage->ny];
                            int *maskPtrZ=&maskPtr[z*targetImage->nx*targetImage->ny];
							for(int y=j*BLOCK_WIDTH; y<(j+1)*BLOCK_WIDTH; y++){
								if(y<targetImage->ny){
									DTYPE *targetPtrXYZ=&targetPtrZ[y*targetImage->nx+i*BLOCK_WIDTH];
                                    int *maskPtrXYZ=&maskPtrZ[y*targetImage->nx+i*BLOCK_WIDTH];
									for(int x=i*BLOCK_WIDTH; x<(i+1)*BLOCK_WIDTH; x++){
										if(x<targetImage->nx && *maskPtrXYZ>-1){
											DTYPE value = *targetPtrXYZ;
											if(value!=0.0)
												variance += (mean - (float)(*targetPtrXYZ))*(mean - float(*targetPtrXYZ));
										}
										targetPtrXYZ++;
									}
								}
							}
						}
					}
					variance /= voxelNumber;
					varianceArray[blockIndex]=variance;
				}
				else{
					varianceArray[blockIndex]=-1;
					unusableBlock++;
				}
				indexArray[blockIndex]=blockIndex;
				blockIndex++;
			}
		}
	}

	params->activeBlockNumber=params->activeBlockNumber<(totalBlockNumber-unusableBlock)?params->activeBlockNumber:(totalBlockNumber-unusableBlock);

	reg_heapSort(varianceArray, indexArray, totalBlockNumber);

	memset(params->activeBlock, 0, totalBlockNumber * sizeof(int));
	int *indexArrayPtr = &indexArray[totalBlockNumber-1];
	int count = 0;
	for(int i=0; i<params->activeBlockNumber; i++){
		params->activeBlock[*indexArrayPtr--] = count++;
	}
	for (int i = params->activeBlockNumber; i < totalBlockNumber; ++i){
		params->activeBlock[*indexArrayPtr--] = -1;
	}

    // renumber them to ensure consistency with the GPU version
    count = 0;
    for (int i = 0; i < totalBlockNumber; ++i)
    {
        if (params->activeBlock[i] != -1)
        {
            params->activeBlock[i] = count;
            ++count;
        }
    }    


	free(varianceArray);
	free(indexArray);
}

void initialise_block_matching_method(  nifti_image * target,
                                        _reg_blockMatchingParam *params,
                                        int percentToKeep_block,
                                        int percentToKeep_opt,
                                        int *mask)
{
	params->blockNumber[0]=(int)ceil((float)target->nx / (float)BLOCK_WIDTH);
	params->blockNumber[1]=(int)ceil((float)target->ny / (float)BLOCK_WIDTH);
	params->blockNumber[2]=(int)ceil((float)target->nz / (float)BLOCK_WIDTH);

	params->percent_to_keep=percentToKeep_opt;
	params->activeBlockNumber=params->blockNumber[0]*params->blockNumber[1]*params->blockNumber[2] * percentToKeep_block / 100;

	params->activeBlock = (int *)malloc(params->blockNumber[0]*params->blockNumber[1]*params->blockNumber[2] * sizeof(int));
	switch(target->datatype){
		case NIFTI_TYPE_UINT8:
			_reg_set_active_blocks<unsigned char>(target, params, mask);break;
		case NIFTI_TYPE_INT8:
			_reg_set_active_blocks<char>(target, params, mask);break;
		case NIFTI_TYPE_UINT16:
			_reg_set_active_blocks<unsigned short>(target, params, mask);break;
		case NIFTI_TYPE_INT16:
			_reg_set_active_blocks<short>(target, params, mask);break;
		case NIFTI_TYPE_UINT32:
			_reg_set_active_blocks<unsigned int>(target, params, mask);break;
		case NIFTI_TYPE_INT32:
			_reg_set_active_blocks<int>(target, params, mask);break;
		case NIFTI_TYPE_FLOAT32:
			_reg_set_active_blocks<float>(target, params, mask);break;
		case NIFTI_TYPE_FLOAT64:
			_reg_set_active_blocks<double>(target, params, mask);break;
		default:
			fprintf(stderr,"ERROR\tinitialise_block_matching_method\tThe target image data type is not supported\n");
			return;
	}
#ifdef _VERBOSE
	printf("[VERBOSE]: There are %i active block(s) out of %i.\n", params->activeBlockNumber, params->blockNumber[0]*params->blockNumber[1]*params->blockNumber[2]);
#endif
	params->targetPosition = (float *)malloc(params->activeBlockNumber*3*sizeof(float));
	params->resultPosition = (float *)malloc(params->activeBlockNumber*3*sizeof(float));
#ifdef _VERBOSE
	printf("[VERBOSE]: block matching initialisation done.\n");
#endif
}
template<typename PrecisionTYPE, typename TargetImageType, typename ResultImageType>
void real_block_matching_method(nifti_image * target,
                                nifti_image * result,
                                _reg_blockMatchingParam *params)
{
	TargetImageType *targetPtr=static_cast<TargetImageType *>(target->data);
	ResultImageType *resultPtr=static_cast<ResultImageType *>(result->data);

	TargetImageType *targetValues=(TargetImageType *)malloc(BLOCK_SIZE*sizeof(TargetImageType));
	bool *targetOverlap=(bool *)malloc(BLOCK_SIZE*sizeof(bool));
	ResultImageType *resultValues=(ResultImageType *)malloc(BLOCK_SIZE*sizeof(ResultImageType));
	bool *resultOverlap=(bool *)malloc(BLOCK_SIZE*sizeof(bool));

	mat44 *targetMatrix_xyz;
	if(target->sform_code >0)
		targetMatrix_xyz = &(target->sto_xyz);
	else targetMatrix_xyz = &(target->qto_xyz);

	int targetIndex_start_x;
	int targetIndex_start_y;
	int targetIndex_start_z;
	int targetIndex_end_x;
	int targetIndex_end_y;
	int targetIndex_end_z;
	int resultIndex_start_x;
	int resultIndex_start_y;
	int resultIndex_start_z;
	int resultIndex_end_x;
	int resultIndex_end_y;
	int resultIndex_end_z;

	unsigned int targetIndex;
	unsigned int resultIndex;

	unsigned int blockIndex=0;
	unsigned int activeBlockIndex=0;

	for(int k=0; k<params->blockNumber[2]; k++){
		targetIndex_start_z=k*BLOCK_WIDTH;
		targetIndex_end_z=targetIndex_start_z+BLOCK_WIDTH;

		for(int j=0; j<params->blockNumber[1]; j++){
			targetIndex_start_y=j*BLOCK_WIDTH;
			targetIndex_end_y=targetIndex_start_y+BLOCK_WIDTH;

			for(int i=0; i<params->blockNumber[0]; i++){
				targetIndex_start_x=i*BLOCK_WIDTH;
				targetIndex_end_x=targetIndex_start_x+BLOCK_WIDTH;

				if(params->activeBlock[blockIndex] >= 0){

					targetIndex=0;
					memset(targetOverlap, 0, BLOCK_SIZE*sizeof(bool));
					for(int z=targetIndex_start_z; z<targetIndex_end_z; z++){
						if(-1<z && z<target->nz){
							TargetImageType *targetPtr_Z = &targetPtr[z*target->nx*target->ny];
							for(int y=targetIndex_start_y; y<targetIndex_end_y; y++){
								if(-1<y && y<target->ny){
									TargetImageType *targetPtr_XYZ = &targetPtr_Z[y*target->nx+targetIndex_start_x];
									for(int x=targetIndex_start_x; x<targetIndex_end_x; x++){
										if(-1<x && x<target->nx){
											TargetImageType value = *targetPtr_XYZ;
											if(value!=0.0){
												targetValues[targetIndex]=value;
												targetOverlap[targetIndex]=1;
											}
										}
										targetPtr_XYZ++;
										targetIndex++;
									}
								}
								else targetIndex+=BLOCK_WIDTH;
							}
						}
						else targetIndex+=BLOCK_WIDTH*BLOCK_WIDTH;
					}
					PrecisionTYPE bestCC=0.0;
					float bestDisplacement[3] = {0.0f};
	
	
					// iteration over the result blocks
					for(int n=-OVERLAP_SIZE; n<OVERLAP_SIZE; n+=STEP_SIZE){
						resultIndex_start_z=targetIndex_start_z+n;
						resultIndex_end_z=resultIndex_start_z+BLOCK_WIDTH;
						for(int m=-OVERLAP_SIZE; m<OVERLAP_SIZE; m+=STEP_SIZE){
							resultIndex_start_y=targetIndex_start_y+m;
							resultIndex_end_y=resultIndex_start_y+BLOCK_WIDTH;
							for(int l=-OVERLAP_SIZE; l<OVERLAP_SIZE; l+=STEP_SIZE){
								resultIndex_start_x=targetIndex_start_x+l;
								resultIndex_end_x=resultIndex_start_x+BLOCK_WIDTH;
								resultIndex=0;
								memset(resultOverlap, 0, BLOCK_SIZE*sizeof(bool));
								for(int z=resultIndex_start_z; z<resultIndex_end_z; z++){
									if(-1<z && z<result->nz){
										ResultImageType *resultPtr_Z = &resultPtr[z*result->nx*result->ny];
										for(int y=resultIndex_start_y; y<resultIndex_end_y; y++){
											if(-1<y && y<result->ny){
												ResultImageType *resultPtr_XYZ = &resultPtr_Z[y*result->nx+resultIndex_start_x];
												for(int x=resultIndex_start_x; x<resultIndex_end_x; x++){
													if(-1<x && x<result->nx){
														ResultImageType value = *resultPtr_XYZ;
														if(value!=0.0){
															resultValues[resultIndex]=value;
															resultOverlap[resultIndex]=1;
														}
													}
													resultPtr_XYZ++;
													resultIndex++;
												}
											}
											else resultIndex+=BLOCK_WIDTH;
										}
									}
									else resultIndex+=BLOCK_WIDTH*BLOCK_WIDTH;
								}
								PrecisionTYPE targetVar=0.0;
								PrecisionTYPE resultVar=0.0;
								PrecisionTYPE targetMean=0.0;
								PrecisionTYPE resultMean=0.0;
								PrecisionTYPE localCC=0.0;
								PrecisionTYPE voxelNumber=0.0;
								for(int a=0; a<BLOCK_SIZE; a++){
									if(targetOverlap[a] && resultOverlap[a]){
										targetMean += (PrecisionTYPE)targetValues[a];
										resultMean += (PrecisionTYPE)resultValues[a];
										voxelNumber++;
									}
								}
	
	
								if(voxelNumber>0){
									targetMean /= voxelNumber;
									resultMean /= voxelNumber;
	
									for(int a=0; a<BLOCK_SIZE; a++){
										if(targetOverlap[a] && resultOverlap[a]){
											PrecisionTYPE targetTemp=(PrecisionTYPE)(targetValues[a]-targetMean);
											PrecisionTYPE resultTemp=(PrecisionTYPE)(resultValues[a]-resultMean);
											targetVar += (targetTemp)*(targetTemp);
											resultVar += (resultTemp)*(resultTemp);
											localCC += (targetTemp)*(resultTemp);
										}
									}
									targetVar = sqrt(targetVar/voxelNumber);
									resultVar = sqrt(resultVar/voxelNumber);
	
									localCC = fabs(localCC/(voxelNumber*targetVar*resultVar));
	
									if(localCC>bestCC){
										bestCC=localCC;
										bestDisplacement[0] = (float)l;
										bestDisplacement[1] = (float)m;
										bestDisplacement[2] = (float)n;
									}
								} 
							}
						}
					}
					
					float targetPosition_temp[3];
					targetPosition_temp[0] = (float)(i*BLOCK_WIDTH);
					targetPosition_temp[1] = (float)(j*BLOCK_WIDTH);
					targetPosition_temp[2] = (float)(k*BLOCK_WIDTH);

					bestDisplacement[0] += targetPosition_temp[0];
					bestDisplacement[1] += targetPosition_temp[1];
					bestDisplacement[2] += targetPosition_temp[2];

					float tempPosition[3];
					apply_affine(targetMatrix_xyz, targetPosition_temp, tempPosition);
					params->targetPosition[activeBlockIndex] = tempPosition[0];
					params->targetPosition[activeBlockIndex+1] = tempPosition[1];
					params->targetPosition[activeBlockIndex+2] = tempPosition[2];
					apply_affine(targetMatrix_xyz, bestDisplacement, tempPosition);
					params->resultPosition[activeBlockIndex] = tempPosition[0];
					params->resultPosition[activeBlockIndex+1] = tempPosition[1];
					params->resultPosition[activeBlockIndex+2] = tempPosition[2];
					activeBlockIndex += 3;
				}
				blockIndex++;
			}
		}
	}

#ifdef _VERBOSE
	double transX=0.0, transY=0.0, transZ=0.0;
	double varX=0.0, varY=0.0, varZ=0.0;
	for (int i = 0; i < params->activeBlockNumber*3; i+=3){
		transX += params->resultPosition[i]-params->targetPosition[i];
		transY += params->resultPosition[i+1]-params->targetPosition[i+1];
		transZ += params->resultPosition[i+2]-params->targetPosition[i+2];
	}
	transX /= (double)params->activeBlockNumber;
	transY /= (double)params->activeBlockNumber;
	transZ /= (double)params->activeBlockNumber;
	for (int i = 0; i < params->activeBlockNumber*3; i+=3){
		varX += (params->resultPosition[i]-params->targetPosition[i] - transX) *
			(params->resultPosition[i]-params->targetPosition[i] - transX);
		varY += (params->resultPosition[i+1]-params->targetPosition[i+1] - transY) *
			(params->resultPosition[i+1]-params->targetPosition[i+1] - transY);
		varZ += (params->resultPosition[i+2]-params->targetPosition[i+2] - transZ) *
			(params->resultPosition[i+2]-params->targetPosition[i+2] - transZ);
	}
	varX /= (double)params->activeBlockNumber;
	varY /= (double)params->activeBlockNumber;
	varZ /= (double)params->activeBlockNumber;
	printf("[VERBOSE] Translation parameters (SD) = [%g(%g) | %g(%g) | %g(%g)]\n",
		transX, sqrt(varX), transY, sqrt(varY), transZ, sqrt(varZ));
#endif
	free(resultValues);
	free(targetValues);
	free(targetOverlap);
	free(resultOverlap);
}

// Block matching interface function
template<typename PrecisionTYPE>
		void block_matching_method(	nifti_image * target,
									nifti_image * result,
									_reg_blockMatchingParam *params)
{
	switch(target->datatype){
		case NIFTI_TYPE_UINT8:
			block_matching_method_2<PrecisionTYPE, unsigned char>
					(target, result, params);
					break;
		case NIFTI_TYPE_INT8:
			block_matching_method_2<PrecisionTYPE, char>
					(target, result, params);
					break;
		case NIFTI_TYPE_UINT16:
			block_matching_method_2<PrecisionTYPE, unsigned short>
					(target, result, params);
					break;
		case NIFTI_TYPE_INT16:
			block_matching_method_2<PrecisionTYPE, short>
					(target, result, params);
					break;
		case NIFTI_TYPE_UINT32:
			block_matching_method_2<PrecisionTYPE, unsigned int>
					(target, result, params);
					break;
		case NIFTI_TYPE_INT32:
			block_matching_method_2<PrecisionTYPE, int>
					(target, result, params);
					break;
		case NIFTI_TYPE_FLOAT32:
			block_matching_method_2<PrecisionTYPE, float>
					(target, result, params);
					break;
		case NIFTI_TYPE_FLOAT64:
			block_matching_method_2<PrecisionTYPE, double>
					(target, result, params);
					break;
		default:
			printf("err\tblock_match\tThe target image data type is not"
					"supported\n");
			return;
	}
}
template void block_matching_method<float>(nifti_image *, nifti_image *, _reg_blockMatchingParam *);
template void block_matching_method<double>(nifti_image *, nifti_image *, _reg_blockMatchingParam *);

// Called internally to determine the parameter type
template<typename PrecisionTYPE, typename TargetImageType> 
		void block_matching_method_2(	nifti_image * target,
										nifti_image * result,
										_reg_blockMatchingParam *params)
{
	switch(result->datatype){
		case NIFTI_TYPE_UINT8:
			real_block_matching_method<PrecisionTYPE, TargetImageType, unsigned char>
					(target, result, params);
					break;
		case NIFTI_TYPE_INT8:
			real_block_matching_method<PrecisionTYPE, TargetImageType, char>
					(target, result, params);
					break;
		case NIFTI_TYPE_UINT16:
			real_block_matching_method<PrecisionTYPE, TargetImageType, unsigned short>
					(target, result, params);
					break;
		case NIFTI_TYPE_INT16:
			real_block_matching_method<PrecisionTYPE, TargetImageType, short>
					(target, result, params);
					break;
		case NIFTI_TYPE_UINT32:
			real_block_matching_method<PrecisionTYPE, TargetImageType, unsigned int>
					(target, result, params);
					break;
		case NIFTI_TYPE_INT32:
			real_block_matching_method<PrecisionTYPE, TargetImageType, int>
					(target, result, params);
					break;
		case NIFTI_TYPE_FLOAT32:
			real_block_matching_method<PrecisionTYPE, TargetImageType, float>
					(target, result, params);
					break;
		case NIFTI_TYPE_FLOAT64:
			real_block_matching_method<PrecisionTYPE, TargetImageType, double>
					(target, result, params);
					break;
		default:
			printf("err\tblock_match\tThe target image data type is not "
					"supported\n");
			return;
	}
}

// Apply the suppled affine transformation to a 3D point
void apply_affine(mat44 * mat, float *pt, float *result)
{	
	result[0] = (mat->m[0][0] * pt[0]) + (mat->m[0][1]*pt[1]) + (mat->m[0][2]*pt[2]) + (mat->m[0][3]);
	result[1] = (mat->m[1][0] * pt[0]) + (mat->m[1][1]*pt[1]) + (mat->m[1][2]*pt[2]) + (mat->m[1][3]);
	result[2] = (mat->m[2][0] * pt[0]) + (mat->m[2][1]*pt[1]) + (mat->m[2][2]*pt[2]) + (mat->m[2][3]);
}



struct _reg_sorted_point
{
	float target[3];
	float result[3];
	
	double distance;
	
	_reg_sorted_point(float * t, float * r, double d)
		:distance(d)
	{
		target[0] = t[0];
		target[1] = t[1];
		target[2] = t[2];
		
		result[0] = r[0];
		result[1] = r[1];
		result[2] = r[2];
	}
	 
	const bool operator <(const _reg_sorted_point & sp) const
	{
		return (sp.distance < distance);
	}
};

// Multiply matrices A and B together and store the result in r.
// We assume that the input pointers are valid and can store the result.
// A = ar * ac
// B = ac * bc
// r = ar * bc

// We can specify if we want to multiply A with the transpose of B

void mul_matrices(float ** a, float ** b, int ar, int ac, int bc, float ** r, bool transposeB)
{
	if (transposeB){
		for (int i = 0; i < ar; ++i){
			for (int j = 0; j < bc; ++j){
				r[i][j] = 0.0f;
				for (int k = 0; k < ac; ++k){
					r[i][j] += a[i][k] * b[j][k];
				}
			}
		}
	}
	else{		
		for (int i = 0; i < ar; ++i){
			for (int j = 0; j < bc; ++j){
				r[i][j] = 0.0f;
				for (int k = 0; k < ac; ++k){
					r[i][j] += a[i][k] * b[k][j];
				}
			}
		}
	}
}

// Multiply a matrix with a vctor
void mul_matvec(float ** a, int ar, int ac, float * b, float * r)
{
	for (int i = 0; i < ar; ++i){
		r[i] = 0;
		for (int k = 0; k < ac; ++k){
			r[i] += a[i][k] * b[k];
		}
	}
}

// Compute determinant of a 3x3 matrix
float compute_determinant3x3(float ** mat)
{
	return 	(mat[0][0]*(mat[1][1]*mat[2][2]-mat[1][2]*mat[2][1]))-
			(mat[0][1]*(mat[1][0]*mat[2][2]-mat[1][2]*mat[2][0]))+
			(mat[0][2]*(mat[1][0]*mat[2][1]-mat[1][1]*mat[2][0]));
}

// estimate an affine transformation using least square
void estimate_affine_transformation(std::vector<_reg_sorted_point> & points,
									mat44 * transformation,
									float ** A,
									float *  w,
									float ** v,
									float ** r,
									float *  b)
{	
	// Create our A matrix
	// Each point will give us 3 linearly independent equations, so
	// we need at least 4 points. Assuming we have that here.
	int num_equations = points.size() * 3;
	unsigned c = 0;
	for (unsigned k = 0; k < points.size(); ++k)
	{
		c = k * 3;
		A[c][0] = points[k].target[0];
		A[c][1] = points[k].target[1];
		A[c][2] = points[k].target[2];
		A[c][3] = A[c][4] = A[c][5] = A[c][6] = A[c][7] = A[c][8] = A[c][10] = A[c][11] = 0.0f;
		A[c][9] = 1.0;
			
		A[c+1][3] = points[k].target[0];
		A[c+1][4] = points[k].target[1];
		A[c+1][5] = points[k].target[2];
		A[c+1][0] = A[c+1][1] = A[c+1][2] = A[c+1][6] = A[c+1][7] = A[c+1][8] = A[c+1][9] = A[c+1][11] = 0.0f;
		A[c+1][10] = 1.0;
			
		A[c+2][6] = points[k].target[0];
		A[c+2][7] = points[k].target[1];
		A[c+2][8] = points[k].target[2];
		A[c+2][0] = A[c+2][1] = A[c+2][2] = A[c+2][3] = A[c+2][4] = A[c+2][5] = A[c+2][9] = A[c+2][10] = 0.0f;
		A[c+2][11] = 1.0;
	}	
	
	for (unsigned k = 0; k < 12; ++k)
	{
		w[k] = 0.0f;
	}	
		// Now we can compute our svd
	svd(A, num_equations, 12, w, v);
		
		// First we make sure that the really small singular values
		// are set to 0. and compute the inverse by taking the reciprocal
		// of the entries
	for (unsigned k = 0; k < 12; ++k)
	{
		if (w[k] < 0.0001)
		{
			w[k] = 0.0f;
		}
		else
		{
			w[k] = 1.0f/w[k];
		}
	}
		
	// Now we can compute the pseudoinverse which is given by
	// V*inv(W)*U'
	// First compute the V * inv(w) in place.
	// Simply scale each column by the corresponding singular value 
	for (unsigned k = 0; k < 12; ++k)
	{
		for (unsigned j = 0; j < 12; ++j)
		{
			v[j][k] *=w[k];
		}
	}
		
	// Now multiply the matrices together
	// Pseudoinverse = v * e * A(transpose)
	mul_matrices(v, A, 12, 12, num_equations, r, true);		
	// Now r contains the pseudoinverse
	// Create vector b and then multiple rb to get the affine paramsA
	for (unsigned k = 0; k < points.size(); ++k)
	{
		c = k * 3;			 
		b[c] = 		points[k].result[0];
		b[c+1] = 	points[k].result[1];
		b[c+2] = 	points[k].result[2];
	}
		
	float * transform = new float[12];
	mul_matvec(r, 12, num_equations, b, transform);
	
	transformation->m[0][0] = transform[0];
	transformation->m[0][1] = transform[1];
	transformation->m[0][2] = transform[2];
	transformation->m[0][3] = transform[9];
		
	transformation->m[1][0] = transform[3];
	transformation->m[1][1] = transform[4];
	transformation->m[1][2] = transform[5];
	transformation->m[1][3] = transform[10];
		
	transformation->m[2][0] = transform[6];
	transformation->m[2][1] = transform[7];
	transformation->m[2][2] = transform[8];
	transformation->m[2][3] = transform[11];
		
	transformation->m[3][0] = 0.0f;
	transformation->m[3][1] = 0.0f;
	transformation->m[3][2] = 0.0f;
	transformation->m[3][3] = 1.0f;

	delete[] transform;
}

void optimize_affine(	_reg_blockMatchingParam *params,
						mat44 * final)
{
	// Set the current transformation to identity
	final->m[0][0] = final->m[1][1] = final->m[2][2] = final->m[3][3] = 1.0f;
	final->m[0][1] = final->m[0][2] = final->m[0][3] = 0.0f;
	final->m[1][0] = final->m[1][2] = final->m[1][3] = 0.0f;
	final->m[2][0] = final->m[2][1] = final->m[2][3] = 0.0f;
	final->m[3][0] = final->m[3][1] = final->m[3][2] = 0.0f;

	const unsigned num_points = params->activeBlockNumber;
	unsigned long num_equations = num_points * 3;
	std::priority_queue<_reg_sorted_point> queue;
	std::vector<_reg_sorted_point> top_points;
	double distance = 0.0;
	double lastDistance = 0.0;
	unsigned long i;

	// massive left hand side matrix
	float ** a = new float *[num_equations];
	for (unsigned k = 0; k < num_equations; ++k)
	{			
		a[k] = new float[12]; // full affine
	}
	
	// The array of singular values returned by svd
	float *w = new float[12];
		
	// v will be n x n
	float **v = new float *[12];
	for (unsigned k = 0; k < 12; ++k)
	{
		v[k] = new float[12];
	}
	
	// Allocate memory for pseudoinverse		
	float **r = new float *[12];
	for (unsigned k = 0; k < 12; ++k)
	{
		r[k] = new float[num_equations];
	}
	
	// Allocate memory for RHS vector
	float *b = new float[num_equations];
	
	// The initial vector with all the input points
	for (unsigned j = 0; j < num_points*3; j+=3)
	{
		top_points.push_back(_reg_sorted_point(&(params->targetPosition[j]), 
							 &(params->resultPosition[j]),0.0f));
	}
	
	// estimate the optimal transformation while considering all the points
	estimate_affine_transformation(top_points, final, a, w, v, r, b);

	// Delete a, b and r. w and v will not change size in subsequent svd operations.
	for (unsigned int k = 0; k < num_equations; ++k)
	{
		delete[] a[k];
	}
	delete[] a;
	delete[] b;
	
	for (unsigned k = 0; k < 12; ++k)
	{
		delete[] r[k];
	}
	delete [] r;


	// The LS in the iterations is done on subsample of the input data	
	float * newResultPosition = new float[num_points*3];
	const unsigned long num_to_keep = (unsigned long)(num_points * (params->percent_to_keep/100.0f));
	num_equations = num_to_keep*3;

	// The LHS matrix
	a = new float *[num_equations];
	for (unsigned k = 0; k < num_equations; ++k)
	{			
		a[k] = new float[12]; // full affine
	}
	
	// Allocate memory for pseudoinverse		
	r = new float *[12];
	for (unsigned k = 0; k < 12; ++k)
	{
		r[k] = new float[num_equations];
	}
	
	// Allocate memory for RHS vector
	b = new float[num_equations];
	
	for (unsigned count = 0; count < MAX_ITERATIONS; ++count)
	{
		// Transform the points in the target
		for (unsigned j = 0; j < num_points * 3; j+=3)		
		{				
			apply_affine(final, &(params->targetPosition[j]), &newResultPosition[j]);
		}

		queue = std::priority_queue<_reg_sorted_point> ();
		for (unsigned j = 0; j < num_points * 3; j+=3)
		{
			distance = get_square_distance(&newResultPosition[j], &(params->resultPosition[j]));
			queue.push(_reg_sorted_point(&(params->targetPosition[j]), 
					   &(params->resultPosition[j]), distance));
		}
						
		distance = 0.0;	
		i = 0;
		top_points.clear();
		while (i < num_to_keep && i < queue.size())
		{
			top_points.push_back(queue.top());
			distance += queue.top().distance;
			queue.pop();
			++i;
		}
				
		// If the change is not 
		/*if (fabs(distance - lastDistance) < 0.001)
		{
			return;
		}*/
		
		lastDistance = distance;
		estimate_affine_transformation(top_points, final, a, w, v, r, b);	
	}
	
	delete[] newResultPosition;
	delete[] b;
	for (unsigned k = 0; k < 12; ++k)
	{
		delete[] r[k];
	}
	delete [] r;
				
	// free the memory
	for (unsigned int k = 0; k < num_equations; ++k)
	{
		delete[] a[k];
	}
	delete[] a;
		
	delete[] w;
	for (int k = 0; k < 12; ++k)
	{
		delete[] v[k];
	}
	delete [] v;	
}

void estimate_rigid_transformation(std::vector<_reg_sorted_point> & points,
								   mat44 * transformation)
{	
	float centroid_target[3] = {0.0f};
	float centroid_result[3] = {0.0f};
	
	
	for (unsigned j = 0; j < points.size(); ++j)
	{
		centroid_target[0] += points[j].target[0];
		centroid_target[1] += points[j].target[1];
		centroid_target[2] += points[j].target[2];
			
		centroid_result[0] += points[j].result[0];
		centroid_result[1] += points[j].result[1];
		centroid_result[2] += points[j].result[2];
	}
		
	centroid_target[0] /= (float)(points.size());
	centroid_target[1] /= (float)(points.size());
	centroid_target[2] /= (float)(points.size());
	
	centroid_result[0] /= (float)(points.size());
	centroid_result[1] /= (float)(points.size());
	centroid_result[2] /= (float)(points.size());
	
	float ** u = new float*[3];
	float * w = new float[3];
	float ** v = new float*[3];
	float ** ut = new float*[3];
	float ** r = new float*[3];

	for (unsigned i = 0; i < 3; ++i)
	{
		u[i] = new float[3];
		v[i] = new float[3];
		ut[i] = new float[3];
		r[i] = new float[3];
		
		w[i] = 0.0f;
	
		
		for (unsigned j = 0; j < 3; ++j)
		{
			u[i][j] = v[i][j] = ut[i][j] = r[i][j] = 0.0f;			
		}
	}
	
	// Demean the input points
	for (unsigned j = 0; j < points.size(); ++j)
	{
		points[j].target[0] -= centroid_target[0];
		points[j].target[1] -= centroid_target[1];
		points[j].target[2] -= centroid_target[2];
			
		points[j].result[0] -= centroid_result[0];
		points[j].result[1] -= centroid_result[1];
		points[j].result[2] -= centroid_result[2];
			
		u[0][0] += points[j].target[0] * points[j].result[0];
		u[0][1] += points[j].target[0] * points[j].result[1];
		u[0][2] += points[j].target[0] * points[j].result[2];
			
		u[1][0] += points[j].target[1] * points[j].result[0];
		u[1][1] += points[j].target[1] * points[j].result[1];
		u[1][2] += points[j].target[1] * points[j].result[2];
			
		u[2][0] += points[j].target[2] * points[j].result[0];
		u[2][1] += points[j].target[2] * points[j].result[1];
		u[2][2] += points[j].target[2] * points[j].result[2];
		
	}
	
	svd(u, 3, 3, w, v);	
	
	// Calculate transpose	
	ut[0][0] = u[0][0];	
	ut[1][0] = u[0][1];
	ut[2][0] = u[0][2];
	
	ut[0][1] = u[1][0];
	ut[1][1] = u[1][1];
	ut[2][1] = u[1][2];
	
	ut[0][2] = u[2][0];
	ut[1][2] = u[2][1];
	ut[2][2] = u[2][2];
	
	// Calculate the rotation matrix
	mul_matrices(v, ut, 3, 3, 3, r, false);
	
	float det = compute_determinant3x3(r);
	
	// Take care of possible reflection 
	if (det < 0.0f)
	{
		v[0][2] = -v[0][2];
		v[1][2] = -v[1][2];
		v[2][2] = -v[2][2];
		
	}
		// Calculate the rotation matrix
	mul_matrices(v, ut, 3, 3, 3, r, false);
	
	// Calculate the translation
	float t[3];
	t[0] = centroid_result[0] - (r[0][0] * centroid_target[0] +
	r[0][1] * centroid_target[1] +
	r[0][2] * centroid_target[2]);
	
	t[1] = centroid_result[1] - (r[1][0] * centroid_target[0] +
	r[1][1] * centroid_target[1] +
	r[1][2] * centroid_target[2]);
	
	t[2] = centroid_result[2] - (r[2][0] * centroid_target[0] +
	r[2][1] * centroid_target[1] +
	r[2][2] * centroid_target[2]);

	
		
	transformation->m[0][0] = r[0][0];
	transformation->m[0][1] = r[0][1];
	transformation->m[0][2] = r[0][2];
	transformation->m[0][3] = t[0];	
		
	transformation->m[1][0] = r[1][0];
	transformation->m[1][1] = r[1][1];
	transformation->m[1][2] = r[1][2];
	transformation->m[1][3] = t[1];
		
	transformation->m[2][0] = r[2][0];
	transformation->m[2][1] = r[2][1];
	transformation->m[2][2] = r[2][2];
	transformation->m[2][3] = t[2];
		
	transformation->m[3][0] = 0.0f;
	transformation->m[3][1] = 0.0f;
	transformation->m[3][2] = 0.0f;
	transformation->m[3][3] = 1.0f;
	
	// Do the deletion here
	for (int i = 0; i < 3; ++i)
	{
		delete [] u[i];
		delete [] v[i];
		delete [] ut[i];
		delete [] r[i];
	}
	delete [] u;
	delete [] v;
	delete [] ut;	
	delete [] r;
	delete [] w;
}


// Find the optimal rigid transformation that will
// bring the point clouds into alignment.
void optimize_rigid(_reg_blockMatchingParam *params,
					mat44 * final)
{	
	unsigned num_points = params->activeBlockNumber;	
	// Keep a sorted list of the distance measure
	std::priority_queue<_reg_sorted_point> queue;
	std::vector<_reg_sorted_point> top_points;
	double distance = 0.0;
	double lastDistance = 0.0;	
	unsigned long i;
	
	// Set the current transformation to identity
	final->m[0][0] = final->m[1][1] = final->m[2][2] = final->m[3][3] = 1.0f;
	final->m[0][1] = final->m[0][2] = final->m[0][3] = 0.0f;
	final->m[1][0] = final->m[1][2] = final->m[1][3] = 0.0f;
	final->m[2][0] = final->m[2][1] = final->m[2][3] = 0.0f;	
	final->m[3][0] = final->m[3][1] = final->m[3][2] = 0.0f;
	
	for (unsigned j = 0; j < num_points * 3; j+= 3)	
	{	
		top_points.push_back(_reg_sorted_point(&(params->targetPosition[j]), 
		&(params->resultPosition[j]), 0.0f));
	}
		
	estimate_rigid_transformation(top_points, final);
	unsigned long num_to_keep = (unsigned long)(num_points * (params->percent_to_keep/100.0f));
	float * newResultPosition = new float[num_points*3];
	
	for (unsigned count = 0; count < MAX_ITERATIONS; ++count)
	{	
		// Transform the points in the target
		for (unsigned j = 0; j < num_points * 3; j+=3)		
		{				
			apply_affine(final, &(params->targetPosition[j]), &newResultPosition[j]);
		}	
		
		queue = std::priority_queue<_reg_sorted_point> ();
		for (unsigned j = 0; j < num_points * 3; j+= 3)
		{			
			distance = get_square_distance(&newResultPosition[j], &(params->resultPosition[j]));
			queue.push(_reg_sorted_point(&(params->targetPosition[j]), 
				&(params->resultPosition[j]), distance));			
		}
						
		distance = 0.0;	
		i = 0;		
		top_points.clear();		
		while (i < num_to_keep && i < queue.size())
		{			
			top_points.push_back(queue.top());						 			
			distance += queue.top().distance;
			queue.pop();
			++i;
		}
				
// 		if (fabs(distance - lastDistance) < 0.001)
// 		{
// 			return;
// 		}		
		
		estimate_rigid_transformation(top_points, final);
	}
	
	delete [] newResultPosition;
}


// Find the optimal affine transformation
void optimize(	_reg_blockMatchingParam *params,
		mat44 * final,
		bool affine)
{	

	if (affine){
		optimize_affine(params, final);
	}
	else{
		optimize_rigid(params, final);
	}	
}

// Routines for alculating Singular Value Decomposition follows.
// Adopted from Numerical Recipes in C.

#define SIGN(a,b) ((b) >= 0.0 ? fabs(a) : -fabs(a))

static float maxarg1,maxarg2;
#define FMAX(a,b) (maxarg1=(a),maxarg2=(b),(maxarg1) > (maxarg2) ?\
        (maxarg1) : (maxarg2))

static int iminarg1,iminarg2;
#define IMIN(a,b) (iminarg1=(a),iminarg2=(b),(iminarg1) < (iminarg2) ?\
        (iminarg1) : (iminarg2))

static float sqrarg;
#define SQR(a) ((sqrarg=(a)) == 0.0 ? 0.0 : sqrarg*sqrarg)

// Calculate pythagorean distance
float pythag(float a, float b)
{
	float absa, absb;
	absa = fabs(a);
	absb = fabs(b);

	if (absa > absb) return (float)(absa * sqrt(1.0f+SQR(absb/absa)));
	else return (absb == 0.0f ? 0.0f : (float)(absb * sqrt(1.0f+SQR(absa/absb))));
}

void svd(float ** in, int m, int n, float * w, float ** v)
{
	float * rv1 = (float *)malloc(sizeof(float) * n);
	float anorm, c, f, g, h, s, scale, x, y, z; 
	int flag,i,its,j,jj,k,l,nm;

	g = scale = anorm = 0.0f;
	for (i = 1; i <= n; ++i)
	{
		l = i + 1;
		rv1[i-1] = scale * g;
		g = s = scale = 0.0f;

		if ( i <= m)
		{
			for (k = i; k <= m; ++k)
			{
				scale += fabs(in[k-1][i-1]);
			}
			if (scale)
			{   
				for (k = i; k <= m; ++k)
				{
					in[k-1][i-1] /= scale;
					s += in[k-1][i-1] * in[k-1][i-1];
				}
				f = in[i-1][i-1];
				g = -SIGN(sqrt(s), f);
				h = f * g - s;
				in[i-1][i-1] = f - g;

				for (j = l; j <= n; ++j)
				{
					for (s = 0.0, k=i; k<=m; ++k) s += in[k-1][i-1]*in[k-1][j-1];				
					f = s/h;
					for (k = i; k <= m; ++k) in[k-1][j-1] += f * in[k-1][i-1];
				}
				for (k = i; k <= m; ++k)
				{
					in[k-1][i-1] *= scale;
				}
			}
		}
		w[i-1] = scale * g;
		g = s = scale = 0.0;
		if ((i <= m) && (i != n))
		{
			for (k = l; k <= n; ++k)
			{
				scale += fabs(in[i-1][k-1]);
			}
			if (scale)
			{
				for (k = l; k <= n; ++k)
				{
					in[i-1][k-1] /= scale;
					s += in[i-1][k-1] * in[i-1][k-1];
				}
				f = in[i-1][l-1];
				g = -SIGN(sqrt(s), f);
				h = f*g-s;
				in[i-1][l-1] = f - g;

				for (k = l; k <= n; ++k) rv1[k-1] = in[i-1][k-1]/h;
				for (j = l; j <= m; ++j)
				{
					for (s = 0.0, k = l; k <= n; ++k)
					{
						s += in[j-1][k-1] * in[i-1][k-1];
					}
					for (k = l; k <= n; ++k)
					{
						in[j-1][k-1] += s * rv1[k-1];
					}
				}

				for (k=l;k<=n;++k) in[i-1][k-1] *= scale;
			}
		}
		anorm = FMAX(anorm, (fabs(w[i-1])+fabs(rv1[i-1])));
	}
    
	for (i = n; i >= 1; --i)
	{
		if (i < n)
		{
			if (g)
			{
				for (j = l; j <= n; ++j)
				{
					v[j-1][i-1] = (in[i-1][j-1]/in[i-1][l-1])/g;
				}
				for (j = l; j <= n; ++j)
				{
					for (s = 0.0, k = l; k <= n; ++k) s += in[i-1][k-1] * v[k-1][j-1];
					for (k=l;k<=n;++k) v[k-1][j-1] += s * v[k-1][i-1];
				}
			}
			for (j = l; j <= n; ++j) v[i-1][j-1] = v[j-1][i-1] = 0.0;
		}
		v[i-1][i-1] = 1.0f;
		g = rv1[i-1];
		l = i;
	}

	for (i = IMIN(m, n); i >= 1; --i)
	{
		l = i+1;
		g = w[i-1];
		for (j = l; j <= n; ++j) in[i-1][j-1] = 0.0f;
		if (g)
		{
			g = 1.0f/g;
			for (j = l; j <= n; ++j)
			{
				for (s = 0.0, k = l; k <= m; ++k) s += in[k-1][i-1] * in[k-1][j-1];
				f = (s/in[i-1][i-1])*g;
				for (k=i; k <=m; ++k) in[k-1][j-1] += f * in[k-1][i-1];
			}
			for (j=i; j <= m; ++j) in[j-1][i-1] *= g;
		}
		else for (j = i; j <= m; ++j) in[j-1][i-1] = 0.0;
		++in[i-1][i-1];
	}

	for (k = n; k >= 1; --k)
	{
		for (its = 0; its < 30; ++its)
		{
			flag = 1;
			for (l=k; l >= 1; --l)
			{
				nm = l - 1;
				if ((float)(fabs(rv1[l-1])+anorm) == anorm)
				{
					flag = 0;
					break;
				}
				if ((float)(fabs(w[nm-1])+anorm) == anorm) break;
			}

			if (flag)
			{
				c = 0.0f;
				s = 1.0f;
				for (i=l; i<=k; ++i) // changed
				{
					f = s * rv1[i-1];
					rv1[i-1] = c * rv1[i-1];
					if ((float)(fabs(f)+anorm) == anorm) break;
					g=w[i-1];
					h=pythag(f,g);
					w[i-1]=h;
					h=1.0f/h;
					c=g*h;
					s = -f*h;

					for (j = 1; j <= m; ++j)
					{
						y=in[j-1][nm-1];
						z=in[j-1][i-1];
						in[j-1][nm-1]=y*c+z*s;
						in[j-1][i-1]=z*c-y*s;
					}
				}
			}
			z = w[k-1];
			if (l == k)
			{
				if (z < 0.0f)
				{
					w[k-1] = -z;
					for (j = 1; j <= n; ++j) v[j-1][k-1] = -v[j-1][k-1];
				}
				break;
			}

			x = w[l-1];
			nm = k - 1;
			y = w[nm-1];
			g = rv1[nm-1];
			h = rv1[k-1];

			f = ((y-z)*(y+z)+(g-h)*(g+h))/(2.0f*h*y);
			g = pythag(f, 1.0f);
			f = ((x-z)*(x+z)+h*((y/(f+SIGN(g,f)))-h))/x;
			c = s = 1.0f;
			for (j = l; j <= nm; ++j)
			{
				i = j + 1;
				g = rv1[i-1];
				y = w[i-1];
				h = s * g;
				g = c * g;
				z = pythag(f, h);
				rv1[j-1] = z;
				c = f/z;
				s = h/z;
				f = x*c+g*s;
				g = g*c-x*s;
				h = y*s;
				y *= c;

				for (jj = 1; jj <= n; ++jj)
				{
					x = v[jj-1][j-1];
					z = v[jj-1][i-1];
					v[jj-1][j-1] = x*c+z*s;
					v[jj-1][i-1] = z*c-x*s;
				}
				z = pythag(f, h);
				w[j-1] = z;
				if (z)
				{
					z = 1.0f/z;
					c = f * z;
					s = h * z;
				}
				f = c*g+s*y;
				x = c*y-s*g;

				for (jj = 1; jj <= m; ++jj)
				{
					y = in[jj-1][j-1];
					z = in[jj-1][i-1];
					in[jj-1][j-1] = y*c+z*s;
					in[jj-1][i-1] = z*c-y*s;
				}
			}
			rv1[l-1] = 0.0f;
			rv1[k-1] = f;
			w[k-1] = x;
		}
	}
	free (rv1);
}