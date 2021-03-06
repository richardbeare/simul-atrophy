#ifndef PETSCADLEMTARAS3D_HXX
#define PETSCADLEMTARAS3D_HXX

#include "PetscAdLemTaras3D.h"
#include "AdLem3D.hxx"

#undef __FUNCT__
#define __FUNCT__ "PetscAdLemTaras3D"
PetscAdLemTaras3D::PetscAdLemTaras3D(AdLem3D<3> *model, bool set12pointStencilForDiv, bool writeParaToFile):
    PetscAdLem3D(model, set12pointStencilForDiv, std::string("Taras Method")),
    mNumOfSolveCalls(0),
    mWriteParaToFile((PetscBool)writeParaToFile)
{
    PetscErrorCode ierr;
    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"dmda of size: (%d,%d,%d)\n",
                            model->getXnum()+1,model->getYnum()+1,model->getZnum()+1);
    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"grid with the spacings hx, hy, hz: (%f,%f,%f)\n",
                            model->getXspacing(),model->getYspacing(),model->getZspacing());
    mParaVecsCreated = PETSC_FALSE;

    if (model->getBcType() == model->DIRICHLET_AT_SKULL)
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"dirichlet_at_skull=>Skull velocity = 0.\n");
    else
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"dirichlet_at_walls =>Skull vel not= 0. Wall vels = provided by user or 0 if not provided.\n");

    if (model->relaxIcInCsf()) {
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Incompressibility constraint relaxed where brain mask has label %d \n"
				,model->getRelaxIcLabel());
	if (fabs(model->getRelaxIcPressureCoeff()) > 1e-6)
	    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Incompressibility constraint relaxation coefficient: %f.\n"
				    ,model->getRelaxIcPressureCoeff());
	else
	    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Incompressibility constraint relaxation coefficient: 1/lambda.\n");
    } else
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Incompressibility constraint not relaxed anywhere.\n");

    if (model->zeroVelAtFalx())
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Falx cerebri voxels set to zero velocity.\n");
    if (model->slidingAtFalx())
    {
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"Sliding boundary condition at Falx cerebri with zero velocity in the direction: %d \n"
				,model->getFalxSlidingZeroVelDir());
    }

    if(mIsMuConstant)
        PetscSynchronizedPrintf(PETSC_COMM_WORLD,"solver uses discretization for constant viscosity case!\n muBrain=%f, muCsf=%f, lambdaBrain=%f, lambdaCsf=%f\n", model->getMuBrain(), model->getMuCsf(), model->getLambdaBrain(), model->getLambdaCsf());
    else
        PetscSynchronizedPrintf(PETSC_COMM_WORLD,"solver takes viscosity values from the image. Note that I still use the discretization for constant/piecewise constant viscosity. The variable viscosity discretization is not supported yet!\n");

    if (model->isLambdaTensor())
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"First Lame Parameter (lambda) is a tensor.\n");
    else
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"First Lame Parameter (lambda) is a scalar.\n");



    //IMPORTANT CHANGE REQUIRED LATER IF USING NON CONSTANT MU:
    //MUST CREATE THE DMDA of stencil width 2 instead of 1 in that case.
    //Easy fix: just put the conditional creation of dmda in the above if(mIsMuConstant) condition!
    //DMDA for only pressure field that is used in Schur Complement solve Sp=rhs.
    const PetscInt stencil_width = (mIsDiv12pointStencil) ? 2 : 1;
    ierr = DMDACreate3d(PETSC_COMM_WORLD,DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,
                        DMDA_STENCIL_BOX,model->getXnum()+1,model->getYnum()+1,model->getZnum()+1,
                        PETSC_DECIDE,PETSC_DECIDE,PETSC_DECIDE,1,stencil_width,0,0,0,&mDaP);CHKERRXX(ierr);

    //    ierr = DMDASetUniformCoordinates(mDaP,0,model->getXnum(),0,model->getYnum(),0,model->getZnum());CHKERRXX(ierr);
    //    ierr = DMDASetUniformCoordinates(mDaP,0,model->getXnum()+2,0,model->getYnum()+2,0,model->getZnum()+2);CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDaP,0,"p");CHKERRXX(ierr);

    ierr = DMDACreate3d(PETSC_COMM_WORLD,DM_BOUNDARY_NONE, DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,
                        DMDA_STENCIL_BOX,model->getXnum()+1,model->getYnum()+1,model->getZnum()+1,
                        PETSC_DECIDE,PETSC_DECIDE,PETSC_DECIDE,4,stencil_width,0,0,0,&mDa);CHKERRXX(ierr);

    //    ierr = DMDASetUniformCoordinates(mDa,0,model->getXnum(),0,model->getYnum(),0,model->getZnum());CHKERRXX(ierr);
    ierr = DMDASetUniformCoordinates(mDa,0,model->getXnum()+1,0,model->getYnum()+1,0,model->getZnum()+1);CHKERRXX(ierr);
    ierr = DMDASetUniformCoordinates(mDa,0,model->getXnum()+2,0,model->getYnum()+2,0,model->getZnum()+2);CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,0,"vx");CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,1,"vy");CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,2,"vz");CHKERRXX(ierr);
    ierr = DMDASetFieldName(mDa,3,"p");CHKERRXX(ierr);


    //Linear Solver context:
    ierr = KSPCreate(PETSC_COMM_WORLD,&mKsp);CHKERRXX(ierr);

    //    createPcForSc();


    if(this->getProblemModel()->relaxIcInCsf()) //non-zero k => no null space present for pressure variable.
	mPressureNullspacePresent = PETSC_FALSE;
    else { //don't relax IC in CSF means k=0 just like in other regions, so there is pressure determined only up to a constant.
	//if(this->getProblemModel()->getRelaxIcPressureCoeff() == 0) {
	mPressureNullspacePresent = PETSC_TRUE;
        setNullSpace();
    }
    mOperatorComputed = PETSC_FALSE;
}

#undef __FUNCT__
#define __FUNCT__ "~PetscAdLemTaras3D"
PetscAdLemTaras3D::~PetscAdLemTaras3D()
{
    PetscErrorCode ierr;
    if(mParaVecsCreated) {
        ierr = VecDestroy(&mAtrophy);CHKERRXX(ierr);
        ierr = VecDestroy(&mMu);CHKERRXX(ierr);
    }

    if(mPressureNullspacePresent) {
        ierr = VecDestroy(&mNullBasis);CHKERRXX(ierr);
        ierr = MatNullSpaceDestroy(&mNullSpace);CHKERRXX(ierr);
        ierr = VecDestroy(&mNullBasisP);CHKERRXX(ierr);
        ierr = MatNullSpaceDestroy(&mNullSpaceP);CHKERRXX(ierr);
    }
    //    ierr = MatDestroy(&mPcForSc);CHKERRXX(ierr);
    ierr = DMDestroy(&mDaP);CHKERRXX(ierr);
}

#undef __FUNCT__
#define __FUNCT__ "setNullSpace"
void PetscAdLemTaras3D::setNullSpace()
{
    // Compute the Null space Basis vector:
    //where all the pressure dof except the ghost pressures are set to 1. Others are set to 0.
    PetscInt ierr;
    ierr = DMCreateGlobalVector(mDa,&mNullBasis);CHKERRXX(ierr);
    DMDALocalInfo info;
    ierr = DMDAGetLocalInfo(mDa,&info);

    PetscAdLemTaras3D::Field    ***nullVec;
    ierr = DMDAVecGetArray(mDa, mNullBasis, &nullVec);CHKERRXX(ierr);

    //FIXME:
    /*if (this->getProblemModel()->getBcType() == this->getProblemModel()->DIRICHLET_AT_WALLS) {
    }*/
    for (PetscInt k=info.zs; k<info.zs+info.zm; ++k) {
        for (PetscInt j=info.ys; j<info.ys+info.ym; ++j) {
            for (PetscInt i=info.xs; i<info.xs+info.xm; ++i) {
                nullVec[k][j][i].vx = 0;
                nullVec[k][j][i].vy = 0;
                nullVec[k][j][i].vz = 0;
                if (
                        (i==0 || j==0 || k==0) //Ghost values
                        || (i==1 && (j==1 || j==info.my-1 || k==1 || k==info.mz-1)) //west wall edges.
                        || (i==info.mx-1 && (j==1 || j==info.my-1 || k==1 || k==info.mz-1)) //east wall edges
                        || (j==1 && (k==1 || k== info.mz-1)) //front wall horizontal edges
                        || (j==info.my-1 && (k==1 || k==info.mz-1))){ //back wall horizontal edges
                    nullVec[k][j][i].p = 0; //FIXME: When setSkullveltozero is used, there are more places to be included here!!
                } else {
                    nullVec[k][j][i].p = 1;
                }

            }
        }
    }
    //FIXME: else if (this->getProblemModel()->getBcType() == this->getProblemModel()->DIRICHLET_AT_SKULL) {
    //SET DIFFERENTLY NULL SPACE HERE.

    ierr = DMDAVecRestoreArray(mDa, mNullBasis, &nullVec);CHKERRXX(ierr);
    ierr = VecAssemblyBegin(mNullBasis);CHKERRXX(ierr);
    ierr = VecAssemblyEnd(mNullBasis);CHKERRXX(ierr);
    ierr = VecNormalize(mNullBasis,NULL);CHKERRXX(ierr);
    //Null Space context:
    ierr = MatNullSpaceCreate(PETSC_COMM_WORLD,PETSC_FALSE,1,&mNullBasis,&mNullSpace);

    ierr = DMCreateGlobalVector(mDaP,&mNullBasisP);CHKERRXX(ierr);

    ierr = VecStrideGather(mNullBasis,3,mNullBasisP,INSERT_VALUES);CHKERRXX(ierr); //insert the fourth field (pos:3, i.e. pressure)
    ierr = VecNormalize(mNullBasisP,NULL);CHKERRXX(ierr);

    //Null Space context:
    ierr = MatNullSpaceCreate(PETSC_COMM_WORLD,PETSC_FALSE,1,&mNullBasisP,&mNullSpaceP);CHKERRXX(ierr);
}

#undef __FUNCT__
#define __FUNCT__ "createPcForSc"
void PetscAdLemTaras3D::createPcForSc()
{
    PetscInt        i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscErrorCode ierr;
    PetscFunctionBeginUser;

    ierr = DMDAGetInfo(mDaP,0,&mx,&my,&mz,0,0,0,0,0,0,0,0,0);CHKERRXX(ierr);
    ierr = DMDAGetCorners(mDaP,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRXX(ierr);

    ierr = DMSetMatrixPreallocateOnly(mDaP,PETSC_TRUE);CHKERRXX(ierr);
//    ierr = DMCreateMatrix(mDaP,MATMPIAIJ,&mPcForSc);CHKERRXX(ierr);
    ierr = DMCreateMatrix(mDaP,&mPcForSc);CHKERRXX(ierr);


    MatStencil row, col;
    PetscReal v;
    row.c = 0;
    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                /*if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1)) //two edges in y-end face
                        ) {
                    continue;
                } else {
                    ++mNumOfZeroDiag;
                }*/
                row.i = i;  row.j = j;  row.k = k;
                col.i = i;  col.j = j;  col.k = k;
                //                v = 1.0/muC(i,j,k);
                v = 0;
                if(!mPressureNullspacePresent) { //FIXME better to use whether we need to relax IC or not ?
                    if(this->bMaskAt(i,j,k) == this->getProblemModel()->getRelaxIcLabel())
                        v = this->getProblemModel()->getRelaxIcPressureCoeff();
                }
                MatSetValuesStencil(mPcForSc,1,&row,1,&col,&v,INSERT_VALUES);CHKERRXX(ierr);
            }
        }
    }
    ierr = MatAssemblyBegin(mPcForSc,MAT_FINAL_ASSEMBLY);CHKERRXX(ierr);
    ierr = MatAssemblyEnd(mPcForSc,MAT_FINAL_ASSEMBLY);CHKERRXX(ierr);
    PetscFunctionReturnVoid();
}

#undef __FUNCT__
#define __FUNCT__ "solveModel"
PetscErrorCode PetscAdLemTaras3D::solveModel(bool operatorChanged)
{
    PetscErrorCode ierr;
    PetscFunctionBeginUser;
    ++mNumOfSolveCalls;

    if(!mOperatorComputed || operatorChanged) { //FIXME: Currently, everytime the operator
        //is changed pc is recomputed. Later see if this is to be done only when null space
        //is required to be computed. otherwise, may be ask not to recompute
        //pc.
        ierr = KSPSetDM(mKsp,mDa);CHKERRQ(ierr);              //mDa with dof = 4, vx,vy,vz and p.
        if(mIsMuConstant) {
//            ierr = DMKSPSetComputeOperators(mDa,computeMatrixTaras3dConstantMu,this);CHKERRQ(ierr);
//            ierr = DMKSPSetComputeRHS(mDa,computeRHSTaras3dConstantMu,this);CHKERRQ(ierr);
            //Using DMKSPSetComputeOperators called computeMatrixTaras3dConstantMu only once, the
            //first time. After that, even when the function DMKSPSetCom... was called, it did not
            //call the computeMatrixTaras3D... to recompute the operator. Using KSPSetComputeOperators
            //below solved this problem.
            ierr = KSPSetComputeOperators(mKsp,computeMatrixTaras3dConstantMu,this);CHKERRQ(ierr);
            ierr = KSPSetComputeRHS(mKsp,computeRHSTaras3dConstantMu,this);CHKERRQ(ierr);
        } else {// TODO: Need to check the correctness of the code discretizing for the variable viscosity case. Commented
	    //below, and using discretization of the (piecewise) constant viscosity case.
            // ierr = DMKSPSetComputeOperators(mDa,computeMatrixTaras3d,this);CHKERRQ(ierr);
            // ierr = DMKSPSetComputeRHS(mDa,computeRHSTaras3d,this);CHKERRQ(ierr);
	    ierr = KSPSetComputeOperators(mKsp,computeMatrixTaras3dConstantMu,this);CHKERRQ(ierr);
            ierr = KSPSetComputeRHS(mKsp,computeRHSTaras3dConstantMu,this);CHKERRQ(ierr);
        }
        ierr = KSPSetFromOptions(mKsp);CHKERRQ(ierr);
	//ierr = KSPSetReusePreconditioner(mKsp, PETSC_FALSE); //This should be called by default when operator changes.
	ierr = KSPSetUp(mKsp);CHKERRQ(ierr); //register the fieldsplits obtained from options.
	// ---------- MUST CALL kspsetfromoptions() and kspsetup() before kspgetoperators and matsetnullspace
	// otherwise I'm getting a runtime error of mat object type not set (for mA!!)
	ierr = KSPGetOperators(mKsp,&mA,NULL);CHKERRQ(ierr);
	// Write the matrix to file: Useful for debugging purpose. Uncomment it if you need to see the matrix.
	// PetscViewer viewer;
	// ierr = PetscViewerCreate(PETSC_COMM_WORLD, &viewer);CHKERRQ(ierr);
	// std::string mat_file("operatorA"+std::to_string(mNumOfSolveCalls)+".output");
	// ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD, mat_file.c_str(),&viewer);CHKERRQ(ierr);
	// MatView(mA, viewer);
	// ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
        if(mPressureNullspacePresent) {
            //ierr = KSPSetNullSpace(mKsp,mNullSpace);CHKERRQ(ierr);//nullSpace for the main system
	    ierr = MatSetNullSpace(mA,mNullSpace);CHKERRQ(ierr);//nullSpace for the main system, updated for petsc3.6
	    PetscBool isNull;
	    if(mPressureNullspacePresent) {
		ierr = MatNullSpaceTest(mNullSpace,mA,&isNull);CHKERRQ(ierr);
		if(!isNull) { //FIXME: Must correct this for skull zero boundary condition!
		    //SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"not a valid system null space \n");
		    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n WARNING: not a valid system null space\n");
		}
	    }
	}
        ierr = KSPGetPC(mKsp,&mPc);CHKERRQ(ierr);

        //Setting up of null spaces and near null spaces for fieldsplits depend upon the kinds of options user have used.
        PetscBool optionFlag = PETSC_FALSE;
        char optionString[PETSC_MAX_PATH_LEN];
        ierr = PetscOptionsGetString(NULL,"-pc_fieldsplit_type",optionString,10,&optionFlag);CHKERRQ(ierr);
        if(optionFlag) {
            if(strcmp(optionString,"schur")==0){
                PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n using schur complement \n");
                ierr = PetscOptionsGetString(NULL,"-pc_fieldsplit_0_fields",optionString,10,&optionFlag);CHKERRQ(ierr);
                if(optionFlag) {
                    if(strcmp(optionString,"0,1,2")==0) {
                        PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n using user defined split \n");
                        //                    if(getProblemModel()->getPressureMassCoeffCsf() == 0) {
                        //                                            ierr = PCFieldSplitSchurPrecondition(mPc,PC_FIELDSPLIT_SCHUR_PRE_USER,mPcForSc);CHKERRQ(ierr);
                        //                    }
                        KSP *subKsp;
                        PetscInt numOfSplits = 1;
                        ierr = PCFieldSplitGetSubKSP(mPc,&numOfSplits,&subKsp);CHKERRQ(ierr);
                        if (numOfSplits != 2) {
                            SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"strange there should be only two splits!! \n");
                        }

                        //If gamg used, set up near-nullspace for fieldsplit_0
                        ierr = PetscOptionsGetString(NULL,"-fieldsplit_0_pc_type",optionString,10,&optionFlag);
                        if(strcmp(optionString,"gamg")==0) {
                            PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n using gamg for A00 \n");
                            //Set up nearNullspace for A00 block.
                            MatNullSpace rigidBodyModes;
                            Vec coords;
                            ierr = DMGetCoordinates(mDa,&coords);CHKERRQ(ierr);
                            ierr = MatNullSpaceCreateRigidBody(coords,&rigidBodyModes);CHKERRQ(ierr);
                            Mat matA00;
                            ierr = KSPGetOperators(subKsp[0],&matA00,NULL);CHKERRQ(ierr);
                            ierr = MatSetNearNullSpace(matA00,rigidBodyModes);CHKERRQ(ierr);
                            ierr = MatNullSpaceDestroy(&rigidBodyModes);CHKERRQ(ierr);
                        }

                        //If constant pressure nullspace present, set it to SchurComplement matrix.
                        if(mPressureNullspacePresent) {
			    PetscBool isNull;
                            Mat matSc;
                            ierr = KSPGetOperators(subKsp[1],&matSc,NULL);CHKERRQ(ierr);
			    //ierr = KSPSetNullSpace(subKsp[1],mNullSpaceP);CHKERRQ(ierr); //no longer used in petsc 3.6
			    ierr = MatSetNullSpace(matSc,mNullSpaceP);CHKERRQ(ierr); //petsc 3.6 update
                            ierr = MatNullSpaceTest(mNullSpaceP,matSc,&isNull);
                            if(!isNull) {//FIXME: Must correct this for skull zero boundary condition!
                                //SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"not a valid pressure null space \n");
				PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n WARNING: not a valid pressure null space\n");
                            }
                        }

                        ierr = PetscFree(subKsp);CHKERRQ(ierr);
                    }
                }
            }
        }
        mOperatorComputed = PETSC_TRUE;
    }

    ierr = KSPSolve(mKsp,NULL,NULL);CHKERRQ(ierr);
    ierr = KSPGetSolution(mKsp,&mX);CHKERRQ(ierr);
    ierr = KSPGetRhs(mKsp,&mB);CHKERRQ(ierr);
    ierr = getSolutionArray();CHKERRQ(ierr); //to get the local solution vector in each processor.
    ierr = getRhsArray();CHKERRQ(ierr);

    PetscFunctionReturn(0);

}

#undef __FUNCT__
#define __FUNCT__ "writeToMatFile"
PetscErrorCode PetscAdLemTaras3D::writeToMatFile(
        const std::string& fileName, bool writeA,
        const std::string& matFileName)
{
    PetscErrorCode ierr;
    PetscFunctionBeginUser;
    PetscViewer viewer1;
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,fileName.c_str(),FILE_MODE_WRITE,&viewer1);CHKERRQ(ierr);
    ierr = PetscViewerSetFormat(viewer1,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);

    ierr = PetscObjectSetName((PetscObject)mX,"x");CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)mB,"b");CHKERRQ(ierr);

    ierr = VecView(mX,viewer1);CHKERRQ(ierr);
    ierr = VecView(mB,viewer1);CHKERRQ(ierr);

    Vec res; //residual
    ierr = VecDuplicate(mX,&res);CHKERRQ(ierr);
    ierr = VecSet(res,0);CHKERRQ(ierr);
    ierr = VecAXPY(res,-1.0,mB);CHKERRQ(ierr);
    ierr = MatMultAdd(mA,mX,res,res);
    ierr = PetscObjectSetName((PetscObject)res,"residual");CHKERRQ(ierr);
    ierr = VecView(res,viewer1);CHKERRQ(ierr);
    ierr = VecDestroy(&res);CHKERRQ(ierr);

    /*ierr = PetscObjectSetName((PetscObject)mNullBasis,"nullBasis");CHKERRQ(ierr);
    ierr = VecView(mNullBasis,viewer1);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)mNullBasisP,"nullBasisP");CHKERRQ(ierr);
    ierr = VecView(mNullBasisP,viewer1);CHKERRQ(ierr);*/


    if(mWriteParaToFile) {
        createParaVectors();
        ierr = PetscObjectSetName((PetscObject)mAtrophy,"atrophy");CHKERRQ(ierr);
        ierr = PetscObjectSetName((PetscObject)mMu,"mu");CHKERRQ(ierr);
        ierr = VecView(mAtrophy,viewer1);CHKERRQ(ierr);
        ierr = VecView(mMu,viewer1);CHKERRQ(ierr);
    }
    ierr = PetscViewerDestroy(&viewer1);CHKERRQ(ierr);

    if(writeA) {
        PetscViewer viewer2;
        ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,matFileName.c_str(),FILE_MODE_WRITE,&viewer2);CHKERRQ(ierr);
        ierr = PetscViewerSetFormat(viewer2,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
        ierr = PetscObjectSetName((PetscObject)mA,"A");CHKERRQ(ierr);
        ierr = MatView(mA,viewer2);CHKERRQ(ierr);
        //        ierr = PetscObjectSetName((PetscObject)mPcForSc,"PcForSc");CHKERRQ(ierr);
        ierr = MatView(mPcForSc,viewer2);CHKERRQ(ierr);
        ierr = PetscViewerDestroy(&viewer2);CHKERRQ(ierr);
    }
    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "createParaVectors"
PetscErrorCode PetscAdLemTaras3D::createParaVectors()
{
    mParaVecsCreated = PETSC_TRUE;
    PetscFunctionBeginUser;
    PetscInt ierr;

    ierr = DMCreateGlobalVector(mDaP,&mAtrophy);CHKERRQ(ierr);
    //    ierr = DMCreateGlobalVector(mDaP,&mMu);CHKERRQ(ierr);
    ierr = VecDuplicate(mAtrophy,&mMu);CHKERRQ(ierr);
    DMDALocalInfo info;
    ierr = DMDAGetLocalInfo(mDaP,&info);

    PetscReal    ***atrophyArray, ***muArray;
    ierr = DMDAVecGetArray(mDaP, mAtrophy, &atrophyArray);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(mDaP, mMu, &muArray);CHKERRQ(ierr);

    for (PetscInt k=info.zs; k<info.zs+info.zm; ++k) {
        for (PetscInt j=info.ys; j<info.ys+info.ym; ++j) {
            for (PetscInt i=info.xs; i<info.xs+info.xm; ++i) {
                atrophyArray[k][j][i] = aC(i,j,k);
                muArray[k][j][i] = muC(i,j,k);
            }
        }
    }

    ierr = DMDAVecRestoreArray(mDaP, mAtrophy, &atrophyArray);CHKERRQ(ierr);
    ierr = VecAssemblyBegin(mAtrophy);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(mAtrophy);CHKERRQ(ierr);

    ierr = DMDAVecRestoreArray(mDaP, mMu, &muArray);CHKERRQ(ierr);
    ierr = VecAssemblyBegin(mMu);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(mMu);CHKERRQ(ierr);
    PetscFunctionReturn(0);
}

MatNullSpace PetscAdLemTaras3D::getNullSpace()
{
    return mNullSpace;
}

#undef __FUNCT__
#define __FUNCT__ "dataCenterAt"

/*AdLem3D class has data only at the cell centers.
The dimension of ghosted cell-centered data is greater by one in each
direction in Taras method. Hence, change the co-ordinate to get proper value
of the data at the cell center.*/
/*This is equivalent to increase the dimension of the image by one by copying the
  value of the faces sharing the origin to it's new neigbhouring face*/
PetscReal PetscAdLemTaras3D::dataCenterAt(std::string dType,
                                          PetscInt x, PetscInt y, PetscInt z,
                                          PetscInt Mi, PetscInt Mj)
{
    if(x != 0)
        --x;
    if(y != 0)
        --y;
    if(z != 0)
        --z;
    return this->getProblemModel()->dataAt(dType,x,y,z,Mi,Mj);
}

PetscReal PetscAdLemTaras3D::bMaskAt(PetscInt x, PetscInt y, PetscInt z)
{
    if(x != 0)
        --x;
    if(y != 0)
        --y;
    if(z != 0)
        --z;
    return this->getProblemModel()->brainMaskAt(x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "dataXyAt"
PetscReal PetscAdLemTaras3D::dataXyAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    //For referencing, easier to say x-start, x-end face, edge etc!
    //on z-end faces: same as the values at z-1!
    if (z == this->getProblemModel()->getZnum())
        --z;

    //x-end and y-end corner point, simply return the corner value.
    if ((x == this->getProblemModel()->getXnum())
            && (y == this->getProblemModel()->getYnum())) {
        return dataCenterAt(dType,x,y,z+1);
    }

    //on x-end faces:
    if (x == this->getProblemModel()->getXnum())
        return 0.5*(dataCenterAt(dType,x,y,z+1) + dataCenterAt(dType,x,y+1,z+1));

    //on y-end faces:
    if (y == this->getProblemModel()->getYnum())
        return 0.5*(dataCenterAt(dType,x,y,z+1) + dataCenterAt(dType,x+1,y,z+1));

    return 0.25 * (dataCenterAt(dType,x,y,z+1) + dataCenterAt(dType,x,y+1,z+1)
                   + dataCenterAt(dType,x+1,y,z+1) + dataCenterAt(dType,x+1,y+1,z+1));
}

#undef __FUNCT__
#define __FUNCT__ "dataXz"
PetscReal PetscAdLemTaras3D::dataXzAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    //on y-end faces: same as the values at y-1!
    if (y == this->getProblemModel()->getYnum())
        --y;

    //x-end and z-end corner point, simply return the corner value.
    if ((x == this->getProblemModel()->getXnum())
            && (z == this->getProblemModel()->getZnum())) {
        return dataCenterAt(dType,x,y+1,z);
    }

    //on x-end faces:
    if (x == this->getProblemModel()->getXnum())
        return 0.5*(dataCenterAt(dType,x,y+1,z) + dataCenterAt(dType,x,y+1,z+1));

    //on z-end faces:
    if (z == this->getProblemModel()->getZnum())
        return 0.5*(dataCenterAt(dType,x,y+1,z) + dataCenterAt(dType,x+1,y+1,z));

    return 0.25 * (dataCenterAt(dType,x,y+1,z) + dataCenterAt(dType,x,y+1,z+1)
                   + dataCenterAt(dType,x+1,y+1,z) + dataCenterAt(dType,x+1,y+1,z+1));
}

#undef __FUNCT__
#define __FUNCT__ "dataYz"
PetscReal PetscAdLemTaras3D::dataYzAt(std::string dType, PetscInt x, PetscInt y, PetscInt z)
{
    //on x-end faces: same as the values at x-1!
    if (x == this->getProblemModel()->getXnum())
        --x;

    //y-end and z-end corner point, simply return the corner value.
    if ((y == this->getProblemModel()->getXnum())
            && (z == this->getProblemModel()->getZnum())) {
        return dataCenterAt(dType,x+1,y,z);
    }

    //on y-end faces:
    if (y == this->getProblemModel()->getXnum())
        return 0.5*(dataCenterAt(dType,x+1,y,z) + dataCenterAt(dType,x+1,y,z+1));

    //on z-end faces:
    if (z == this->getProblemModel()->getZnum())
        return 0.5*(dataCenterAt(dType,x+1,y,z) + dataCenterAt(dType,x+1,y+1,z));

    return 0.25 * (dataCenterAt(dType,x+1,y,z) + dataCenterAt(dType,x+1,y,z+1)
                   + dataCenterAt(dType,x+1,y+1,z) + dataCenterAt(dType,x+1,y+1,z+1));
}

#undef __FUNCT__
#define __FUNCT__ "muC"
PetscReal PetscAdLemTaras3D::muC(PetscInt x, PetscInt y, PetscInt z)
{
    return dataCenterAt("mu",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "lambdaC"
PetscReal PetscAdLemTaras3D::lambdaC(PetscInt x, PetscInt y, PetscInt z,
                                     PetscInt Mi, PetscInt Mj)
{
    return dataCenterAt("lambda",x,y,z,Mi,Mj);
}

#undef __FUNCT__
#define __FUNCT__ "aC"
PetscReal PetscAdLemTaras3D::aC(PetscInt x, PetscInt y, PetscInt z)
{
    return dataCenterAt("atrophy",x,y,z);
}

#undef __FUNCT__
#define __FUNCT_ "muXy"
PetscReal PetscAdLemTaras3D::muXy(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXyAt("mu",x,y,z);
}

#undef __FUNCT_
#define __FUNCT__ "muXz"
PetscReal PetscAdLemTaras3D::muXz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXzAt("mu",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "muYz"
PetscReal PetscAdLemTaras3D::muYz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataYzAt("mu",x,y,z);
}

#undef __FUNCT__
#define __FUNCT_ "lambdaXy"
PetscReal PetscAdLemTaras3D::lambdaXy(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXyAt("lambda",x,y,z);
}

#undef __FUNCT_
#define __FUNCT__ "lambdaXz"
PetscReal PetscAdLemTaras3D::lambdaXz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataXzAt("lambda",x,y,z);
}

#undef __FUNCT__
#define __FUNCT__ "lambdaYz"
PetscReal PetscAdLemTaras3D::lambdaYz(PetscInt x, PetscInt y, PetscInt z)
{
    return dataYzAt("lambda",x,y,z);
}



#undef __FUNCT__
#define __FUNCT__ "computeMatrixTaras3d"
PetscErrorCode PetscAdLemTaras3D::computeMatrixTaras3d(
        KSP ksp, Mat J, Mat jac, void *ctx)
{
    PetscAdLemTaras3D *user = (PetscAdLemTaras3D*)ctx;

    PetscErrorCode  ierr;
    PetscInt        i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscReal       Hx,Hy,Hz,HyHzdHx,HxHzdHy,HxHydHz;
    PetscScalar     v[17];
    MatStencil      row, col[17];
    DM              da;
    PetscReal       kBond = 1.0; //need to change it to scale the coefficients.
    PetscReal       kCont = 1.0; //need to change it to scale the coefficients.

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da,0,&mx,&my,&mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx = user->getProblemModel()->getXspacing();//0.95;//1;//1./(mx-1);
    Hy = user->getProblemModel()->getYspacing();//0.95; //1;//1./(my-1);
    Hz = user->getProblemModel()->getZspacing();//1.5; //;//1./(mz-1);

    HyHzdHx = (Hy*Hz)/Hx;
    HxHzdHy = (Hx*Hz)/Hy;
    HxHydHz = (Hx*Hy)/Hz;
    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);

    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                row.i = i; row.j = j; row.k = k;
                // ********************* x-momentum equation *******************
                row.c = 0;
                //Ghost vx unknowns(j=my-1,k=mz-1);boundary vx:(i=0,i=mx-1,j=0,j=my-2,k=0,k=mz-2)
                if (i==0 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vx. So let's
                    //initiate the component for these:
                    col[0].c = 0;       col[1].c = 0;

                    //x-start and x-end faces: vx(i,j,k) = 0
                    if (i==0 || i==mx-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //y-start, y-end; z-start, z-end faces:
                        //ghost values for j=my-1, k=mz-1: vx(i,j,k) = 0
                        if (j==my-1 || k==mz-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (j==0 || j==my-2) {//y-start and y-end faces
                                //3*vx(i,0,k) - vx(i,1,k) = 0; 3*vx(i,my-2,k) - vx(i,my-3,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (j==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                                }
                                else if (j==my-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //z-start and z-end faces
                                //3*vx(i,j,0) - vx(i,j,1) = 0; 3*vx(i,j,mz-2) - vx(i,j,mz-3) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (k==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                                }
                                else if (k==mz-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, x-momentum equation
                    //vx-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 0;

                    v[0] = 2.0*user->muC(i+1,j+1,k+1)*HyHzdHx;      col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = 2.0*user->muC(i,j+1,k+1)*HyHzdHx;        col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = user->muXy(i,j+1,k)*HxHzdHy;             col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = user->muXy(i,j,k)*HxHzdHy;               col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = user->muXz(i,j,k+1)*HxHydHz;             col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = user->muXz(i,j,k)*HxHydHz;               col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -v[0]-v[1]-v[2]-v[3]-v[4]-v[5];          col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //vy-coefficients, four terms.
                    for(int ii=7; ii<11; ++ii)
                        col[ii].c = 1;

                    v[7] = user->muXy(i,j+1,k)*Hz;                  col[7].i=i;     col[7].j=j+1;   col[7].k=k;
                    v[8] = -v[7];                                   col[8].i=i-1;   col[8].j=j+1;   col[8].k=k;
                    v[9] = user->muXy(i,j,k)*Hz;                    col[9].i=i-1;   col[9].j=j;     col[9].k=k;
                    v[10] = -v[9];                                  col[10].i=i;    col[10].j=j;    col[10].k=k;

                    //vz-coefficients, four terms.
                    for(int ii=11; ii<15; ++ii)
                        col[ii].c = 2;

                    v[11] = user->muXz(i,j,k+1)*Hy;                 col[11].i=i;    col[11].j=j;    col[11].k=k+1;
                    v[12] = -v[11];                                 col[12].i=i-1;  col[12].j=j;    col[12].k=k+1;
                    v[13] = user->muXz(i,j,k)*Hy;                   col[13].i=i-1;  col[13].j=j;    col[13].k=k;
                    v[14] = -v[13];                                 col[14].i=i;    col[14].j=j;    col[14].k=k;

                    //p-coefficients, two terms.
                    col[15].c = 3;      col[16].c = 3;
                    v[15] = kCont*Hy*Hz;        col[15].i=i;    col[15].j=j+1;  col[15].k=k+1;
                    v[16] = -v[15];             col[16].i=i+1;  col[16].j=j+1;  col[16].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,17,col,v,INSERT_VALUES);
                }
                // -*********************** y-momentum equation *******************
                row.c = 1;
                //Ghost vy unknowns(x=mx-1,k=mz-1);boundary vy:(i=0,i=mx-2,j=0,j=my-1,k=0,k=mz-2)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vy. So let's
                    //initiate the component for these:
                    col[0].c = 1;       col[1].c = 1;

                    //y-start and y-end faces: vy(i,j,k) = 0
                    if (j==0 || j==my-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //x-start, x-end; z-start, z-end faces:
                        //ghost values for i=mx-1, k=mz-1: vy(i,j,k) = 0
                        if (i==mx-1 || k==mz-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (i==0 || i==mx-2) {//x-start and x-end faces
                                //3*vy(0,j,k) - vy(1,j,k) = 0; 3*vy(mx-2,j,k) - vy(mx-3,j,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (i==0) {
                                    v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                                }
                                else if (i==mx-2) {
                                    v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //z-start and z-end faces
                                //3*vy(i,j,0) - vy(i,j,1) = 0; 3*vy(i,j,mz-2) - vy(i,j,mz-3) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (k==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                                }
                                else if (k==mz-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, y-momentum equation
                    //vy-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 1;

                    v[0] = 2.0*user->muC(i+1,j+1,k+1)*HxHzdHy;      col[0].i=i;     col[0].j=j+1;   col[0].k=k;
                    v[1] = 2.0*user->muC(i+1,j,k+1)*HxHzdHy;        col[1].i=i;     col[1].j=j-1;   col[1].k=k;
                    v[2] = user->muXy(i+1,j,k)*HyHzdHx;             col[2].i=i+1;   col[2].j=j;     col[2].k=k;
                    v[3] = user->muXy(i,j,k)*HyHzdHx;               col[3].i=i-1;   col[3].j=j;     col[3].k=k;
                    v[4] = user->muYz(i,j,k+1)*HxHydHz;             col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = user->muYz(i,j,k)*HxHydHz;               col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -v[0]-v[1]-v[2]-v[3]-v[4]-v[5];          col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //vx-coefficients, four terms.
                    for(int ii=7; ii<11; ++ii)
                        col[ii].c = 0;

                    v[7] = user->muXy(i+1,j,k)*Hz;                  col[7].i=i+1;   col[7].j=j;     col[7].k=k;
                    v[8] = -v[7];                                   col[8].i=i+1;   col[8].j=j-1;   col[8].k=k;
                    v[9] = user->muXy(i,j,k)*Hz;                    col[9].i=i;     col[9].j=j-1;   col[9].k=k;
                    v[10] = -v[9];                                  col[10].i=i;    col[10].j=j;    col[10].k=k;

                    //vz-coefficients, four terms.
                    for(int ii=11; ii<15; ++ii)
                        col[ii].c = 2;

                    v[11] = user->muYz(i,j,k+1)*Hx;                 col[11].i=i;    col[11].j=j;    col[11].k=k+1;
                    v[12] = -v[11];                                 col[12].i=i;    col[12].j=j-1;  col[12].k=k+1;
                    v[13] = user->muYz(i,j,k)*Hx;                   col[13].i=i;    col[13].j=j-1;  col[13].k=k;
                    v[14] = -v[13];                                 col[14].i=i;    col[14].j=j;    col[14].k=k;

                    //p-coefficients, two terms.
                    col[15].c = 3;      col[16].c = 3;
                    v[15] = kCont*Hx*Hz;        col[15].i=i+1;  col[15].j=j;    col[15].k=k+1;
                    v[16] = -v[15];             col[16].i=i+1;  col[16].j=j+1;  col[16].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,17,col,v,INSERT_VALUES);
                }

                // -*********************** z-momentum equation *******************
                row.c = 2;
                //Ghost vz unknowns(x=mx-1,y=my-1);boundary vz:(i=0,i=mx-2,j=0,j=my-2,k=0,k=mz-1)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1) {
                    //all boundary/ghost conditions use at most two terms and for only vz. So let's
                    //initiate the component for these:
                    col[0].c = 2;       col[1].c = 2;

                    //z-start and z-end faces: vz(i,j,k) = 0
                    if (k==0 || k==mz-1) { //boundary values
                        v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else { //x-start, x-end; y-start, y-end faces:
                        //ghost values for i=mx-1, j=my-1: vz(i,j,k) = 0
                        if (i==mx-1 || j==my-1) {
                            v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                            ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                        } else {
                            if (i==0 || i==mx-2) {//x-start and x-end faces
                                //3*vz(0,j,k) - vz(1,j,k) = 0; 3*vz(mx-2,j,k) - vz(mx-3,j,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (i==0) {
                                    v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                                }
                                else if (i==mx-2) {
                                    v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            } else { //y-start and y-end faces
                                //3*vz(i,0,k) - vz(i,1,k) = 0; 3*vz(i,my-2,k) - vz(i,my-3,k) = 0
                                v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                                if (j==0) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                                }
                                else if (j==my-2) {
                                    v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                                }
                                ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                            }
                        }
                    }
                } else { //interior points, z-momentum equation
                    //vz-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii)
                        col[ii].c = 2;

                    v[0] = 2.0*user->muC(i+1,j+1,k+1)*HxHydHz;      col[0].i=i;     col[0].j=j;     col[0].k=k+1;
                    v[1] = 2.0*user->muC(i+1,j+1,k)*HxHydHz;        col[1].i=i;     col[1].j=j;     col[1].k=k-1;
                    v[2] = user->muXz(i+1,j,k)*HyHzdHx;             col[2].i=i+1;   col[2].j=j;     col[2].k=k;
                    v[3] = user->muXz(i,j,k)*HyHzdHx;               col[3].i=i-1;   col[3].j=j;     col[3].k=k;
                    v[4] = user->muYz(i,j+1,k)*HxHzdHy;             col[4].i=i;     col[4].j=j+1;   col[4].k=k;
                    v[5] = user->muYz(i,j,k)*HxHzdHy;               col[5].i=i;     col[5].j=j-1;   col[5].k=k;
                    v[6] = -v[0]-v[1]-v[2]-v[3]-v[4]-v[5];          col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //vx-coefficients, four terms.
                    for(int ii=7; ii<11; ++ii)
                        col[ii].c = 0;

                    v[7] = user->muXz(i+1,j,k)*Hy;                  col[7].i=i+1;   col[7].j=j;     col[7].k=k;
                    v[8] = -v[7];                                   col[8].i=i+1;   col[8].j=j;     col[8].k=k-1;
                    v[9] = user->muXz(i,j,k)*Hy;                    col[9].i=i;     col[9].j=j;     col[9].k=k-1;
                    v[10] = -v[9];                                  col[10].i=i;    col[10].j=j;    col[10].k=k;

                    //vy-coefficients, four terms.
                    for(int ii=11; ii<15; ++ii)
                        col[ii].c = 1;

                    v[11] = user->muYz(i,j+1,k)*Hx;                 col[11].i=i;    col[11].j=j+1;  col[11].k=k;
                    v[12] = -v[11];                                 col[12].i=i;    col[12].j=j+1;  col[12].k=k-1;
                    v[13] = user->muYz(i,j,k)*Hx;                   col[13].i=i;    col[13].j=j;    col[13].k=k-1;
                    v[14] = -v[13];                                 col[14].i=i;    col[14].j=j;    col[14].k=k;

                    //p-coefficients, two terms.
                    col[15].c = 3;      col[16].c = 3;
                    v[15] = kCont*Hx*Hy;        col[15].i=i+1;  col[15].j=j+1;  col[15].k=k;
                    v[16] = -v[15];             col[16].i=i+1;  col[16].j=j+1;  col[16].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,17,col,v,INSERT_VALUES);CHKERRQ(ierr);

                }

                // -********************** continuity equation *********************
                row.c = 3;
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1)) //two edges in y-end face
                        //|| (i==2 && j==2 && k==1) //constant pressure point NOT USED. INSTEAD TELL PETSC ABOUT THIS CONSTANT NULL-SPACE PRESSURE
                        ) {//BY DOING PCFIELDSPLIT. FIXME: MIGHT BE BETTER TO USE PCFIELDSPLIT EXPLICITLY HERE IN THE SOLUTION THAN LETTING IT AS
                    //COMMAND LINE OPTION SINCE WE MUST USE PCFIELDSPLIT IN THIS CASE!

                    //For all the ghost and boundary conditions we need at most two terms for p.
                    col[0].c = 3;       col[1].c = 3;
                    if (i==0 || j==0 || k==0) { //Ghost pressure p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i; col[0].j=j; col[0].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) {//four edges in x-start face.
                        //set dp/dx=0 i.e. p(i+1,j,k) - p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) { //four edges in x-end face.
                        //set dp/dx=0 i.e. p(i,j,k) - p(i-1,j,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (j==1 && (k==1 || k== mz-1)) { //two edges in y-start face.
                        //set dp/dy=0 i.e. p(i,j+1,k) - p(i,j,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j+1;   col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j;     col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } else if (j==my-1 && (k==1 || k==mz-1)) { //two edges in y-end face
                        //set dp/dy=0 i.e. p(i,j,k) - p(i,j-1,k) = 0;
                        v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                        v[1] = -kBond;      col[1].i=i;     col[1].j=j-1;   col[1].k=k;
                        ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    } //else { //one cell NOTE: RHS needs to be set to kBond*pCell;
                    //  v[0] = kBond;       col[0].i=i;     col[0].j=j;     col[0].k=k;
                    //  ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                    //}
                } else {
                    //vx-coefficients, two terms
                    col[0].c = 0;       col[1].c = 0;
                    v[0] = kCont/Hx;    col[0].i = i;   col[0].j=j-1;   col[0].k=k-1;
                    v[1] = -v[0];       col[1].i = i-1; col[1].j=j-1;   col[1].k=k-1;

                    //vy-coefficients, two terms
                    col[2].c = 1;       col[3].c = 1;
                    v[2] = kCont/Hy;    col[2].i = i-1; col[2].j=j;     col[2].k=k-1;
                    v[3] = -v[2];       col[3].i = i-1; col[3].j=j-1;   col[3].k=k-1;

                    //vz-coefficients, two terms
                    col[4].c = 2;       col[5].c = 2;
                    v[4] = kCont/Hz;    col[4].i = i-1; col[4].j=j-1;   col[4].k=k;
                    v[5] = -v[4];       col[5].i = i-1; col[5].j=j-1;   col[5].k=k-1;

                    ierr=MatSetValuesStencil(jac,1,&row,6,col,v,INSERT_VALUES);CHKERRQ(ierr);
                }
            }
        }
    }
    ierr = MatAssemblyBegin(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "computeRHSTaras3d"
PetscErrorCode PetscAdLemTaras3D::computeRHSTaras3d(KSP ksp, Vec b, void *ctx)
{
    PetscAdLemTaras3D    *user = (PetscAdLemTaras3D*)ctx;
    PetscErrorCode ierr;
    PetscInt       i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscScalar    Hx,Hy,Hz;
    PetscAdLemTaras3D::Field    ***rhs;
    DM             da;
    PetscReal       kCont=1.0;

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da, 0, &mx, &my, &mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx   = 1;//1.0 / (PetscReal)(mx-1);
    Hy   = 1;//1.0 / (PetscReal)(my-1);
    Hz   = 1;//1.0 / (PetscReal)(mz-1);

    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(da, b, &rhs);CHKERRQ(ierr);

    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                //Ghost vx unknowns(j=my-1,k=mz-1);boundary vx:(i=0,i=mx-1,j=0,j=my-2,k=0,k=mz-2)
                if (i==0 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    rhs[k][j][i].vx = 0;
                } else { //interior points, x-momentum equation
                    rhs[k][j][i].vx = Hy*Hz*(user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1)
                                             +user->lambdaC(i+1,j+1,k+1,0,0) + user->lambdaC(i,j+1,k+1,0,0)
                                             )*(user->aC(i+1,j+1,k+1) - user->aC(i,j+1,k+1))/2.0;
                }
                // *********************** y-momentum equation *******************
                //Ghost vy unknowns(x=mx-1,k=mz-1);boundary vy:(i=0,i=mx-2,j=0,j=my-1,k=0,k=mz-2)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-1 || k==0 || k==mz-2 || k==mz-1) {
                    rhs[k][j][i].vy = 0;
                } else { //interior points, y-momentum equation
                    rhs[k][j][i].vy = Hx*Hz*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1)
                                             +user->lambdaC(i+1,j+1,k+1,0,0) + user->lambdaC(i+1,j,k+1,0,0)
                                             )*(user->aC(i+1,j+1,k+1) - user->aC(i+1,j,k+1))/2.0;
                }

                // *********************** z-momentum equation *******************
                //Ghost vz unknowns(x=mx-1,y=my-1);boundary vz:(i=0,i=mx-2,j=0,j=my-2,k=0,k=mz-1)
                if (i==0 || i==mx-2 || i==mx-1 || j==0 || j==my-2 || j==my-1 || k==0 || k==mz-1) {
                    rhs[k][j][i].vz = 0;
                } else { //interior points, z-momentum equation
                    rhs[k][j][i].vz = Hx*Hy*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k)
                                             +user->lambdaC(i+1,j+1,k+1,0,0) + user->lambdaC(i+1,j+1,k,0,0)
                                             )*(user->aC(i+1,j+1,k+1) - user->aC(i+1,j+1,k))/2.0;
                }

                //  ********************** continuity equation *********************
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-start face.
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //four edges in x-end face.
                        || (j==1 && (k==1 || k== mz-1)) //two edges in y-start face.
                        || (j==my-1 && (k==1 || k==mz-1))) { //two edges in y-end face
                    rhs[k][j][i].p = 0;
                } //else if (i==2 && j==1 && k==1) {//constant pressure point
                //  rhs[k][j][i].p = kCont*user->getP0Cell();
                //}
                else {
                    rhs[k][j][i].p = -kCont*user->aC(i,j,k);
                }
            }
        }
    }

    ierr = DMDAVecRestoreArray(da, b, &rhs);CHKERRQ(ierr);
    //    ierr = VecAssemblyBegin(b);CHKERRQ(ierr);
    //    ierr = VecAssemblyEnd(b);CHKERRQ(ierr);
    //    ierr = MatNullSpaceRemove(user->getNullSpace(),b,NULL);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "computeMatrixTaras3dConstantMu"
PetscErrorCode PetscAdLemTaras3D::computeMatrixTaras3dConstantMu(
        KSP ksp, Mat J, Mat jac, void *ctx)
{
    PetscAdLemTaras3D *user = (PetscAdLemTaras3D*)ctx;

    PetscErrorCode  ierr;
    PetscInt        i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscReal       Hx,Hy,Hz,HyHzdHx,HxHzdHy,HxHydHz;
    const unsigned int nnz = (user->isDiv12pointStencil()) ? 13 : 9;
    PetscScalar     v[nnz];
    MatStencil      row, col[nnz];
    DM              da;
    PetscReal       kBond = 1.0; //need to change it to scale the coefficients.
    PetscReal       kCont = 1.0; //need to change it to scale the coefficients.

    PetscFunctionBeginUser;
    if(user->getProblemModel()->noLameInRhs())
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n RHS will be taken as grad(a), i.e without Lame parameters.\n");
    else
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n RHS will be taken as (mu + lambda)grad(a), i.e. with Lame parameters.\n");

    PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n computing the operator for linear solve with %d point stencil for divergence\n", (nnz-1));
    if (PetscAdLemTaras3D_SolverOps::RELAX_IC_WITH_ZERO_ROWS)
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n Relax IC with zero rows corresponding to cells where IC is to be relaxed.\n", (nnz-1));
    else
	PetscSynchronizedPrintf(PETSC_COMM_WORLD,"\n Relax IC with div(u) + kp = 0 on relax cells\n", (nnz-1));
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da,0,&mx,&my,&mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx = user->getProblemModel()->getXspacing();
    Hy = user->getProblemModel()->getYspacing();
    Hz = user->getProblemModel()->getZspacing();
    //PetscSynchronizedPrintf(PETSC_COMM_WORLD,"grid spacings hx, hy, hz: (%f, %f, %f)\n", Hx, Hy, Hz);
    HyHzdHx = (Hy*Hz)/Hx;
    HxHzdHy = (Hx*Hz)/Hy;
    HxHydHz = (Hx*Hy)/Hz;
    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);

    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                row.i = i; row.j = j; row.k = k;
                // ********************* x-momentum equation *******************
                row.c = 0;
                col[0].c = 0;       col[1].c = 0; //boundary points, has at most two coeffs.

                if(j==my-1 || k==mz-1) {     //back and north wall ghost nodes:
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(i==0 || i==mx-1) { //west and east walls: vx = wx and vx = ex
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(j==0 || j==my-2) { //front and back wall 3*vx(i,j,k) - vx(i,j+-1,k) = 2fx or 2bx
                    v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                    if (j==0) {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                    }
                    else {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                    }
                    ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(k==0 || k==mz-2) { //south and north wall: //3*vx(i,j,k) - vx(i,j,k+-1) = 2sx or 2nx
                    v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                    if (k==0) {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                    }
                    else {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                    }
                    ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if( (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getSkullLabel() ||
                             user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getSkullLabel())
		    ) { //vx lying in the face that touches a skull (non-brain region) cell
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if( (user->getProblemModel()->zeroVelAtFalx()) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vx lying in the face that touches a Falx Cerebri cell
		    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
		} else if( (user->getProblemModel()->slidingAtFalx() && (user->getProblemModel()->getFalxSlidingZeroVelDir()==0)) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vx lying in the face that touches a Falx Cerebri cell
		    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
		}
                else { //interior points, x-momentum equation
                    //vx-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii) col[ii].c = 0;
                    PetscScalar coeff = (user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1))/2.;
                    v[0] = coeff*HyHzdHx;          col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = coeff*HyHzdHx;          col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = coeff*HxHzdHy;          col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = coeff*HxHzdHy;          col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = coeff*HxHydHz;          col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = coeff*HxHydHz;          col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -2*(v[0]+v[2]+v[4]);    col[6].i=i;     col[6].j=j;     col[6].k=k;
                    //p-coefficients, two terms.
                    col[7].c = 3;      col[8].c = 3;
                    v[7] = kCont*Hy*Hz;        col[7].i=i;    col[7].j=j+1;  col[7].k=k+1;
                    v[8] = -v[7];              col[8].i=i+1;  col[8].j=j+1;  col[8].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,9,col,v,INSERT_VALUES);
                }
                //*********************** y-momentum equation *******************
                row.c = 1;
                col[0].c = 1;       col[1].c = 1;   //boundary points, at most two coeffs.
                if(i==mx-1 || k==mz-1) {   //east and north ghost walls.
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(j==0 || j==my-1) { //front or back wall: vy = fy or by
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(i==0 || i==mx-2) { //west or east wall: //3*vy(i,j,k) - vy(i+or-1,j,k) = 2wy or 2ey
                    v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                    if (i==0) {
                        v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                    }
                    else {
                        v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                    }
                    ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(k==0 || k==mz-2) { //south or north wall:3*vy(i,j,k) - vy(i,j,k+-1) = 2sy or 2ny
                    v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                    if (k==0) {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k+1;
                    }
                    else {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j;   col[1].k=k-1;
                    }
                    ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if ( (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getSkullLabel() ||
                             user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getSkullLabel())
                            ) { //vy lying in the face that touches a skull (non-brain region) cell
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if( (user->getProblemModel()->zeroVelAtFalx()) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vy lying in the face that touches a Falx Cerebri cell
		    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
		} else if( (user->getProblemModel()->slidingAtFalx() && (user->getProblemModel()->getFalxSlidingZeroVelDir()==1)) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vy lying in the face that touches a Falx Cerebri cell
		    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
		}
                else { //interior points, y-momentum equation
                    //vy-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii) col[ii].c = 1;
                    PetscScalar coeff = (user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1))/2.;
                    v[0] = coeff*HyHzdHx;          col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = coeff*HyHzdHx;          col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = coeff*HxHzdHy;          col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = coeff*HxHzdHy;          col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = coeff*HxHydHz;          col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = coeff*HxHydHz;          col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -2*(v[0]+v[2]+v[4]);    col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //p-coefficients, two terms.
                    col[7].c = 3;      col[8].c = 3;
                    v[7] = kCont*Hx*Hz;       col[7].i=i+1;  col[7].j=j;    col[7].k=k+1;
                    v[8] = -v[7];             col[8].i=i+1;  col[8].j=j+1;  col[8].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,9,col,v,INSERT_VALUES);
                }

                //*********************** z-momentum equation *******************
                row.c = 2;
                col[0].c = 2;       col[1].c = 2;       //at most two points for boundary nodes.

                if(i==mx-1 || j==my-1) {   //east and back ghost walls.
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(k==0 || k==mz-1) { //south or north walls: vz = sz or nz
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(i==0 || i==mx-2) { //west or east wall: 3vz(i,j,k)-vz(i+-1,j,k)=2wz or 2ez
                    v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                    if (i==0) {
                        v[1] = -kBond;      col[1].i = i+1; col[1].j = j;   col[1].k=k;
                    }
                    else if (i==mx-2) {
                        v[1] = -kBond;      col[1].i = i-1; col[1].j = j;   col[1].k=k;
                    }
                    ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if(j==0 || j==my-2) { //front or back wall: 3vz(i,j,k)-vz(i,j+-1,k)=2fz or 2bz
                    v[0] = 3*kBond;         col[0].i = i;   col[0].j = j;   col[0].k=k;
                    if (j==0) {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j+1; col[1].k=k;
                    }
                    else if (j==my-2) {
                        v[1] = -kBond;      col[1].i = i;   col[1].j = j-1; col[1].k=k;
                    }
                    ierr=MatSetValuesStencil(jac,1,&row,2,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if ( (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getSkullLabel() ||
                             user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getSkullLabel())
                            ) { //vz lying in the face that touches a skull (non-brain region) cell
                    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if( (user->getProblemModel()->zeroVelAtFalx()) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vz lying in the face that touches a Falx Cerebri cell
		    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
		} else if( (user->getProblemModel()->slidingAtFalx() && (user->getProblemModel()->getFalxSlidingZeroVelDir()==2)) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vz lying in the face that touches a Falx Cerebri cell
		    v[0] = kBond;           col[0].i = i;   col[0].j = j;   col[0].k = k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
		}
                else { //interior points, z-momentum equation
                    //vz-coefficients, seven terms.
                    for(int ii=0; ii<7; ++ii) col[ii].c = 2;
                    PetscScalar coeff = (user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k))/2.;
                    v[0] = coeff*HyHzdHx;          col[0].i=i+1;   col[0].j=j;     col[0].k=k;
                    v[1] = coeff*HyHzdHx;          col[1].i=i-1;   col[1].j=j;     col[1].k=k;
                    v[2] = coeff*HxHzdHy;          col[2].i=i;     col[2].j=j+1;   col[2].k=k;
                    v[3] = coeff*HxHzdHy;          col[3].i=i;     col[3].j=j-1;   col[3].k=k;
                    v[4] = coeff*HxHydHz;          col[4].i=i;     col[4].j=j;     col[4].k=k+1;
                    v[5] = coeff*HxHydHz;          col[5].i=i;     col[5].j=j;     col[5].k=k-1;
                    v[6] = -2*(v[0]+v[2]+v[4]);    col[6].i=i;     col[6].j=j;     col[6].k=k;

                    //p-coefficients, two terms.
                    col[7].c = 3;      col[8].c = 3;
                    v[7] = kCont*Hx*Hy;        col[7].i=i+1;  col[7].j=j+1;  col[7].k=k;
                    v[8] = -v[7];              col[8].i=i+1;  col[8].j=j+1;  col[8].k=k+1;

                    ierr=MatSetValuesStencil(jac,1,&row,9,col,v,INSERT_VALUES);CHKERRQ(ierr);
                }
		// PetscSynchronizedPrintf(PETSC_COMM_WORLD,"(%d,%d,%d): s=%d, m=%d\n",
		// 			i, j, k, (int)user->bMaskAt(i, j, k), (int)user->muC(i, j, k));

                //********************** continuity equation *********************
                row.c = 3;
                col[0].c = 3;       //boundary or ghost nodes; at most one point.
                if (i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //west wall corners
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //east wall corners
                        || (j==1 && (k==1 || k==mz-1)) //front wall horizontal corners
                        || (j==my-1 && (k==1 || k==mz-1)) //back wall horizontal corners
                        ) {
                    v[0] = kBond;       col[0].i=i; col[0].j=j; col[0].k=k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                } else if (
		    (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                           (user->bMaskAt(i,j,k) == user->getProblemModel()->getSkullLabel() ||
                             ( user->bMaskAt(i+1,j,k) == user->getProblemModel()->getSkullLabel() &&
                               user->bMaskAt(i-1,j,k) == user->getProblemModel()->getSkullLabel() &&
                               user->bMaskAt(i,j+1,k) == user->getProblemModel()->getSkullLabel() &&
                               user->bMaskAt(i,j-1,k) == user->getProblemModel()->getSkullLabel() &&
                               user->bMaskAt(i,j,k+1) == user->getProblemModel()->getSkullLabel() &&
                               user->bMaskAt(i,j,k-1) == user->getProblemModel()->getSkullLabel()
                             )
                           )
                          ) { //Skull cell or a non-skull cell surrounded by skull cells in all its 6-neigbhor.
                    v[0] = kBond;       col[0].i=i; col[0].j=j; col[0].k=k;
                    ierr=MatSetValuesStencil(jac,1,&row,1,col,v,INSERT_VALUES);CHKERRQ(ierr);
                }
                else {
		    PetscReal common_coeff;
		    if(user->isDiv12pointStencil())
			common_coeff = kCont/4.;
		    else
			common_coeff = kCont;
		    int nm = 0;
		    //vx-coefficients, two or four terms
		    PetscReal coeff = common_coeff/Hx;
		    if(user->isDiv12pointStencil() && i<mx-2)
		    {
			col[nm].c = 0; col[nm].i = i+1;   col[nm].j=j-1;   col[nm].k=k-1;
			v[nm++] = coeff;
		    }
                    col[nm].c = 0; col[nm].i = i;   col[nm].j=j-1;   col[nm].k=k-1; v[nm++] = coeff;
		    col[nm].c = 0; col[nm].i = i-1; col[nm].j=j-1;   col[nm].k=k-1; v[nm++] = -1*coeff;
		    if(user->isDiv12pointStencil() && i>2)
		    {
			col[nm].c = 0; col[nm].i = i-2; col[nm].j=j-1;   col[nm].k=k-1; v[nm++] = -1*coeff;
		    }

                    //vy-coefficients, two or four terms
		    coeff = common_coeff/Hy;
		    if(user->isDiv12pointStencil() && j<my-2)
		    {
			col[nm].c = 1; col[nm].i = i-1; col[nm].j=j+1;     col[nm].k=k-1;
			v[nm++] = coeff;
		    }
                    col[nm].c = 1; col[nm].i = i-1; col[nm].j=j;     col[nm].k=k-1; v[nm++] = coeff;
		    col[nm].c = 1; col[nm].i = i-1; col[nm].j=j-1;   col[nm].k=k-1; v[nm++] = -1 * coeff;
		    if(user->isDiv12pointStencil() && j>2)
		    {
			col[nm].c = 1; col[nm].i = i-1; col[nm].j=j-2;   col[nm].k=k-1; v[nm++] = -1 * coeff;
		    }
                    //vz-coefficients, two or four terms
		    coeff = common_coeff/Hz;
		    if(user->isDiv12pointStencil() && k<mz-2)
		    {
			col[nm].c = 2; col[nm].i = i-1; col[nm].j=j-1;   col[nm].k=k+1;
			v[nm++] = coeff;
		    }
                    col[nm].c = 2; col[nm].i = i-1; col[nm].j=j-1;   col[nm].k=k; v[nm++] = coeff;
		    col[nm].c = 2; col[nm].i = i-1; col[nm].j=j-1;   col[nm].k=k-1; v[nm++] = -1*coeff;
		    if(user->isDiv12pointStencil() && k>2)
		    {
			col[nm].c = 2; col[nm].i = i-1; col[nm].j=j-1;   col[nm].k=k-2; v[nm++] = -1*coeff;
		    }

		    // Pressure coefficient k whose value depends on whether IC is to be relaxed in this row or not.
		    col[nm].c = 3; col[nm].i = i;   col[nm].j = j;   col[nm].k = k;
                    if((user->getProblemModel()->relaxIcInCsf()) &&
                            (user->bMaskAt(i,j,k) == user->getProblemModel()->getRelaxIcLabel()))
		    { //If relax IC option set and if this row corresponds to the IC relaxation cell.
			// Non-integer type RelaxIcLabel could be used to adapt the compressibilty based on partial volumes
			// However, the variation in k in the eqn div(u) + kp = 0 required to have desirable effect is so big
			// that this would perhaps only work if the partial volumes are scaled with some function to get desired
			// big change in k. that is k = vol_fraction_of_IClabel * f(vol_fraction) might work if we find
			// suitable f(vol_fraction). This is not done here currently, need to test several cases to make this work.
			if (!PetscAdLemTaras3D_SolverOps::RELAX_IC_WITH_ZERO_ROWS)
			{   // Relax IC by setting a non-zero coeff. of pressure variable.
			    if (fabs(user->getProblemModel()->getRelaxIcPressureCoeff()) < 1e-6)
				v[nm++] = 1./user->lambdaC(i, j, k, 0, 0);
			    else
				v[nm++] = user->getProblemModel()->getRelaxIcPressureCoeff();
			    ierr=MatSetValuesStencil(jac,1,&row,nm,col,v,INSERT_VALUES);CHKERRQ(ierr);
			}
			else
			{ //Relax IC with zero rows i.e. NO div(u) + kp = 0!
			    //Set the row explicitly to zero. DON'T leave by just skipping setting zero because this can create problem for  multiple time-step solve.
			    //Some of the rows corresponding to non-relax_ic cells in the previous solve might be a relax_ic cell now. In these cells the previous non-zero
			    //values are not modified if I don't explicitly set them to zero!!!
			    SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_PLIB,"Relax IC by setting zero rows not supported yet.\n");
			    //I need to figure out a safer way to set the whole row to zero without disturbing the non-zero pattern first.
			}
                    }
		    else if( (user->getProblemModel()->zeroVelAtFalx() ||  user->getProblemModel()->slidingAtFalx()) &&
			     (user->bMaskAt(i,j,k) == user->getProblemModel()->getFalxCerebriLabel())
			)
		    { //If falx cerebri, just release the IC. This is different than what I did for skull cells above setting
			//p=0. Instead of setting p=0 in the Falx, I'm just releasing the strict IC, using coeff. 1
			v[nm++] = 1.0;
			ierr=MatSetValuesStencil(jac,1,&row,nm,col,v,INSERT_VALUES);CHKERRQ(ierr);
		    }
		    else
		    {
			v[nm++] = 0; //Must set 0 since when doing multiple solves, there can be non-zero rows in previous time-step that now corrsponds to relax_ic cells
			ierr=MatSetValuesStencil(jac,1,&row,nm,col,v,INSERT_VALUES);CHKERRQ(ierr);
		    }
                }
            }
        }
    }
    ierr = MatAssemblyBegin(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "computeRHSTaras3dConstantMu"
PetscErrorCode PetscAdLemTaras3D::computeRHSTaras3dConstantMu(KSP ksp, Vec b, void *ctx)
{
    PetscAdLemTaras3D    *user = (PetscAdLemTaras3D*)ctx;
    std::vector<double> wallVel(18);
    user->getProblemModel()->getWallVelocities(wallVel);
    //indices for the walls, then use 0,1,2 as offsets to get vx,vy and vz respectively.
    unsigned int sWall(0), wWall(3), nWall(6), eWall(9), fWall(12), bWall(15);

    PetscErrorCode ierr;
    PetscInt       i,j,k,mx,my,mz,xm,ym,zm,xs,ys,zs;
    PetscScalar    Hx,Hy,Hz;
    PetscAdLemTaras3D::Field    ***rhs;
    DM             da;
    PetscReal      kCont=1.0;

    PetscFunctionBeginUser;
    ierr = KSPGetDM(ksp,&da);CHKERRQ(ierr);
    ierr = DMDAGetInfo(da, 0, &mx, &my, &mz,0,0,0,0,0,0,0,0,0);CHKERRQ(ierr);
    Hx = user->getProblemModel()->getXspacing();
    Hy = user->getProblemModel()->getYspacing();
    Hz = user->getProblemModel()->getZspacing();

    // Hx   = 1;//1.0 / (PetscReal)(mx-1);
    // Hy   = 1;//1.0 / (PetscReal)(my-1);
    // Hz   = 1;//1.0 / (PetscReal)(mz-1);

    //Gradient of A.
    PetscReal gradAx;
    PetscReal gradAy;
    PetscReal gradAz;

    ierr = DMDAGetCorners(da,&xs,&ys,&zs,&xm,&ym,&zm);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(da, b, &rhs);CHKERRQ(ierr);

    for (k=zs; k<zs+zm; ++k) {
        for (j=ys; j<ys+ym; ++j) {
            for (i=xs; i<xs+xm; ++i) {
                //---*********** Compute gradient ----*************************//
                if (i<mx-1 && j<my-1 && k<mz-1) {
                    gradAx = (user->aC(i+1,j+1,k+1) - user->aC(i,j+1,k+1));
                    gradAy = (user->aC(i+1,j+1,k+1) - user->aC(i+1,j,k+1));
                    gradAz = (user->aC(i+1,j+1,k+1) - user->aC(i+1,j+1,k));
                }
                //---****************** x-momentum equation ********************---//
                if(j==my-1 || k==mz-1) {     //back and north wall ghost nodes:
                    rhs[k][j][i].vx = 0;
                } else if(i==0) {       //west wall:    wx
                    rhs[k][j][i].vx = wallVel.at(wWall);
                } else if(i==mx-1) {    //east wall:    ex
                    rhs[k][j][i].vx = wallVel.at(eWall);
                } else if(j==0) {       //front wall:   2fx
                    rhs[k][j][i].vx = 2*wallVel.at(fWall);
                } else if(j==my-2) {    //back wall:   2bx
                    rhs[k][j][i].vx = 2*wallVel.at(bWall);
                } else if(k==0) {       //south wall:   2sx
                    rhs[k][j][i].vx = 2*wallVel.at(sWall);
                } else if(k==mz-2) {    //north wall:    2nx
                    rhs[k][j][i].vx = 2*wallVel.at(nWall);
                } else if ( (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getSkullLabel() ||
                             user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getSkullLabel())
                            ) { //skull or non-brain region:
                    rhs[k][j][i].vx = 0;
                } else if( (user->getProblemModel()->zeroVelAtFalx()) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vx lying in the face that touches a Falx Cerebri cell
		    rhs[k][j][i].vx = 0;
		} else if( (user->getProblemModel()->slidingAtFalx() && (user->getProblemModel()->getFalxSlidingZeroVelDir()==0)) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vx lying in the face that touches a Falx Cerebri cell
		    rhs[k][j][i].vx = 0;
		}
                else { //interior points, x-momentum equation
		    if (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getRelaxIcLabel() || user->bMaskAt(i,j+1,k+1) == user->getProblemModel()->getRelaxIcLabel())
			rhs[k][j][i].vx = 0; //no force when gradAx computed using at least one value from CSF voxel.
                    else if(user->getProblemModel()->noLameInRhs()) //make force independent of Lame parameters.
			rhs[k][j][i].vx = Hy*Hz*gradAx;
		    else if (user->getProblemModel()->isLambdaTensor()) {
                        rhs[k][j][i].vx = Hy*Hz*( gradAx*0.5*(user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1)) +
						  user->lambdaC(i,j,k,0,0)*gradAx + user->lambdaC(i,j,k,0,1)*gradAy + user->lambdaC(i,j,k,0,2)*gradAz
			    );
                    } else {
                        // rhs[k][j][i].vx = Hy*Hz*(
                        //             user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1) +
                        //             user->lambdaC(i+1,j+1,k+1,0,0) +
                        //             user->lambdaC(i,j+1,k+1,0,0)
                        //             ) * gradAx / 2.0;
			//If we use force in CSF as above, and if we set lambda_csf = lambda_tissue
			// changing mu (= mu_csf = mu_tissue) will have absolutely no impact on
			// the deformation field obtained and it only scales the pressure values in the tissue.
			// But once we comment out above and use no force in CSF, lambda_csf is never used and
			// changing mu (=mu_csf=mu_tissue) changes the deformation field obtained.
			// This is perhaps because using f_csf as above and with lambda_csf = lambda_tissue will
			// create equal balancing force in csf voxels next to the tissue since with non-zero grad(a).
			//rhs[k][j][i].vx = Hy*Hz*(user->muC(i,j,k) + user->lambdaC(i,j,k,0,0)) * gradAx;
			rhs[k][j][i].vx = Hy*Hz*gradAx*( 0.5*(user->muC(i+1,j+1,k+1) + user->muC(i,j+1,k+1)) +
							 user->lambdaC(i,j,k,0,0)
			    );
		    }
		}

                //---*********************** y-momentum equation *******************---//
                if(i==mx-1 || k==mz-1) {   //east and north ghost walls.
                    rhs[k][j][i].vy = 0;
                } else if(j==0) {       //front wall:   fy
                    rhs[k][j][i].vy = wallVel.at(fWall+1);
                } else if(j==my-1) {    //back wall:   by
                    rhs[k][j][i].vy = wallVel.at(bWall+1);
                } else if(i==0) {       //west wall:    2wy
                    rhs[k][j][i].vy = 2*wallVel.at(wWall+1);
                } else if(i==mx-2) {    //east wall:    2ey
                    rhs[k][j][i].vy = 2*wallVel.at(eWall+1);
                } else if(k==0) {       //south wall:   2sy
                    rhs[k][j][i].vy = 2*wallVel.at(sWall+1);
                } else if(k==mz-2) {    //north wall:    2ny
                    rhs[k][j][i].vy = 2*wallVel.at(nWall+1);
                } else if ( (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getSkullLabel() ||
                             user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getSkullLabel())
                            ) { //Skull or non-brain region:
                    rhs[k][j][i].vy = 0;
                } else if( (user->getProblemModel()->zeroVelAtFalx()) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vy lying in the face that touches a Falx Cerebri cell
		    rhs[k][j][i].vy = 0;
		} else if( (user->getProblemModel()->slidingAtFalx() && (user->getProblemModel()->getFalxSlidingZeroVelDir()==1)) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vy lying in the face that touches a Falx Cerebri cell
		    rhs[k][j][i].vy = 0;
		}
                else { //interior points, y-momentum equation
		    if (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getRelaxIcLabel() || user->bMaskAt(i+1,j,k+1) == user->getProblemModel()->getRelaxIcLabel())
			rhs[k][j][i].vy = 0; //no force when gradAy computed using at least one value from CSF voxel.
                    else if(user->getProblemModel()->noLameInRhs()) //make force independent of Lame parameters.
			rhs[k][j][i].vy = Hx*Hz*gradAy;
                    else if (user->getProblemModel()->isLambdaTensor()) {
                        rhs[k][j][i].vy = Hx*Hz*( gradAy*0.5*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1)) +
						  user->lambdaC(i,j,k,1,0)*gradAx + user->lambdaC(i,j,k,1,1)*gradAy + user->lambdaC(i,j,k,1,2)*gradAz
			    );
                    }else {
                    //     rhs[k][j][i].vy = Hx*Hz*(
                    //                 user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1) +
                    //                 user->lambdaC(i+1,j+1,k+1,0,0) + user->lambdaC(i+1,j,k+1,0,0)
                    //                          )*gradAy/2.0;
                    // }
			//If we use force in CSF as above, and if we set lambda_csf = lambda_tissue
			// changing mu (= mu_csf = mu_tissue) will have absolutely no impact on
			// the deformation field obtained and it only scales the pressure values in the tissue.
			// But once we comment out above and use no force in CSF, lambda_csf is never used and
			// changing mu (=mu_csf=mu_tissue) changes the deformation field obtained.
			// This is perhaps because using f_csf as above and with lambda_csf = lambda_tissue will
			// create equal balancing force in csf voxels next to the tissue since with non-zero grad(a).
			//rhs[k][j][i].vy = Hx*Hz*(user->muC(i,j,k) + user->lambdaC(i,j,k,0,0)) * gradAy;
			rhs[k][j][i].vy = Hx*Hz*gradAy*( 0.5*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j,k+1)) +
							 user->lambdaC(i,j,k,0,0)
			    );
		    }
                }

                //--*********************** z-momentum equation *******************--//
                if(i==mx-1 || j==my-1) {   //east and back ghost walls.
                    rhs[k][j][i].vz = 0;
                } else if(k==0) {       //south wall:   sz
                    rhs[k][j][i].vz = wallVel.at(sWall+2);
                } else if(k==mz-1) {    //north wall:    nz
                    rhs[k][j][i].vz = wallVel.at(nWall+2);
                } else if(i==0) {       //west wall:    2wz
                    rhs[k][j][i].vz = 2*wallVel.at(wWall+2);
                } else if(i==mx-2) {    //east wall:    2ez
                    rhs[k][j][i].vz = 2*wallVel.at(eWall+2);
                } else if(j==0) {       //front wall:   2fz
                    rhs[k][j][i].vz = 2*wallVel.at(fWall+2);
                } else if(j==my-2) {    //back wall:   2bz
                    rhs[k][j][i].vz = 2*wallVel.at(bWall+2);
                } else if ( (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            ( user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getSkullLabel() ||
                              user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getSkullLabel()
                            )
                          ) { //skull or non-brain region:
                    rhs[k][j][i].vz = 0;
                } else if( (user->getProblemModel()->zeroVelAtFalx()) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vz lying in the face that touches a Falx Cerebri cell
		    rhs[k][j][i].vz = 0;
		} else if( (user->getProblemModel()->slidingAtFalx() && (user->getProblemModel()->getFalxSlidingZeroVelDir()==2)) &&
			   (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getFalxCerebriLabel() ||
			    user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getFalxCerebriLabel())
		    ) { //vz lying in the face that touches a Falx Cerebri cell
		    rhs[k][j][i].vz = 0;
		}
                else { //interior points, z-momentum equation
		    if (user->bMaskAt(i+1,j+1,k+1) == user->getProblemModel()->getRelaxIcLabel() || user->bMaskAt(i+1,j+1,k) == user->getProblemModel()->getRelaxIcLabel())
			rhs[k][j][i].vz = 0; //no force when gradAz computed using at least one value from CSF voxel.
                    else if(user->getProblemModel()->noLameInRhs()) //make force independent of Lame parameters.
			rhs[k][j][i].vz = Hx*Hy*gradAz;
                    else if (user->getProblemModel()->isLambdaTensor()) {
                        rhs[k][j][i].vz = Hx*Hy*( gradAz*0.5*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k)) +
						  user->lambdaC(i,j,k,2,0)*gradAx + user->lambdaC(i,j,k,2,1)*gradAy + user->lambdaC(i,j,k,2,2)*gradAz
			    );
                    } else {
                    //     rhs[k][j][i].vz = Hx*Hy*(
                    //                 user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k) +
                    //                 user->lambdaC(i+1,j+1,k+1,0,0) + user->lambdaC(i+1,j+1,k,0,0)
                    //                             )*gradAz/2.0;
                    // }
			//If we use force in CSF as above, and if we set lambda_csf = lambda_tissue
			// changing mu (= mu_csf = mu_tissue) will have absolutely no impact on
			// the deformation field obtained and it only scales the pressure values in the tissue.
			// But once we comment out above and use no force in CSF, lambda_csf is never used and
			// changing mu (=mu_csf=mu_tissue) changes the deformation field obtained.
			// This is perhaps because using f_csf as above and with lambda_csf = lambda_tissue will
			// create equal balancing force in csf voxels next to the tissue since with non-zero grad(a).
			//rhs[k][j][i].vz = Hx*Hy*(user->muC(i,j,k) + user->lambdaC(i,j,k,0,0)) * gradAz;
			rhs[k][j][i].vz = Hx*Hy*gradAz*( 0.5*(user->muC(i+1,j+1,k+1) + user->muC(i+1,j+1,k))
							 + user->lambdaC(i,j,k,0,0)
			    );
		    }
                }

                //  ********************** continuity equation *********************
                if(i==0 || j==0 || k==0 //Ghost values
                        || (i==1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //west wall corners
                        || (i==mx-1 && (j==1 || j==my-1 || k==1 || k==mz-1)) //east wall corners
                        || (j==1 && (k==1 || k== mz-1)) //front wall corners
                        || (j==my-1 && (k==1 || k==mz-1)) //back wall corners
                        ) {
                    rhs[k][j][i].p = 0;
                }  else if (
		    (user->getProblemModel()->getBcType() == user->getProblemModel()->DIRICHLET_AT_SKULL) &&
                            (user->bMaskAt(i,j,k) == user->getProblemModel()->getSkullLabel() ||
                              ( user->bMaskAt(i+1,j,k) == user->getProblemModel()->getSkullLabel() &&
                                user->bMaskAt(i-1,j,k) == user->getProblemModel()->getSkullLabel() &&
                                user->bMaskAt(i,j+1,k) == user->getProblemModel()->getSkullLabel() &&
                                user->bMaskAt(i,j-1,k) == user->getProblemModel()->getSkullLabel() &&
                                user->bMaskAt(i,j,k+1) == user->getProblemModel()->getSkullLabel() &&
                                user->bMaskAt(i,j,k-1) == user->getProblemModel()->getSkullLabel()
                              )
                            )
                           ) { //Skull cell or a non-skull cell surrounded by skull cells in all its 6-neigbhor.
                    rhs[k][j][i].p = 0;
                }
                else {
                    rhs[k][j][i].p = -kCont*user->aC(i,j,k);
                }
            }
        }
    }

    ierr = DMDAVecRestoreArray(da, b, &rhs);CHKERRQ(ierr);
    //    ierr = VecAssemblyBegin(b);CHKERRQ(ierr);
    //    ierr = VecAssemblyEnd(b);CHKERRQ(ierr);
    //    ierr = MatNullSpaceRemove(user->getNullSpace(),b,NULL);CHKERRQ(ierr);

    PetscFunctionReturn(0);
}
#endif // PETSCADLEMTARAS3D_HXX
