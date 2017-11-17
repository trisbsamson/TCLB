#include "acRemoteForceInterface.h"
std::string acRemoteForceInterface::xmlname = "RemoteForceInterface";
#include "../HandlerFactory.h"


int acRemoteForceInterface::Init () {
        Action::Init();
        xcirc = false;
        particle_type = "NRotSphere";
        sim = "sim";
        gridSpacing = 25.0;
        verletDist = 5.0;
        double sx,sy,sz; // Size of the domain for ESYS
        pugi::xml_attribute attr = node.attribute("particle");
        if (attr) particle_type = attr.value();
        if (particle_type == "NRotSphere") {
        } else if (particle_type == "RotSphere") {
        } else {
                ERROR("Unknown particle type in ESYS\n");
                return -1;
        }
        attr = node.attribute("gridSpacing");
        if (attr) gridSpacing = attr.as_double();
        attr = node.attribute("verletDist");
        if (attr) verletDist = attr.as_double();
        attr = node.attribute("esys-object");
        if (attr) sim = attr.value();
        attr = node.attribute("periodic");
        if (attr) {
                if (strcmp(attr.value(),"x") == 0) {
                        xcirc = true;
                } else if (strcmp(attr.value(),"") == 0) {
                        xcirc = false;
                } else {
                        ERROR("ESYS-Particles can be only periodic in X direction\n");
                        return -1;
                }
        }
                
        sx = solver->mpi.totalregion.nx;
        sy = solver->mpi.totalregion.ny;
        sz = solver->mpi.totalregion.nz;
        
        int workers = solver->lattice->RFI.space_for_workers() - 1;
        if (workers < 1) {
                ERROR("ESYS-P: No place for workers (you need at least 2 additionals processes)\n");
                return -1;
        }
        int nx=1, ny=1, nz=1;
        {
                int nx0, ny0, nz0;
                int nx1, ny1, nz1;
                int tot, tot1;
                tot=0;
                nx0 = solver->mpi.divx;
                ny0 = solver->mpi.divy;
                nz0 = solver->mpi.divz;
                output("%dx%dx%d\n",nx0, ny0, nz0);
                for (int nx1=1; nx1<=nx0; nx1++) if (nx0 % nx1 == 0) {
                        for (int ny1=1; ny1<=ny0; ny1++) if (ny0 % ny1 == 0) {
                                for (int nz1=1; nz1<=nz0; nz1++) if (nz0 % nz1 == 0) {
                                        int tot1 = nx1*ny1*nz1;
                                        if (tot1 <= workers) {
                                                if (workers % tot1 == 0) {
                                                        if (tot1 > tot) {
                                                                tot = tot1;
                                                                nx = workers / (ny1 * nz1);
                                                                ny = ny1;
                                                                nz = nz1;
                                                        }
                                                }
                                        }
                                }
                        }
                }
                if (tot == 0) {
                        ERROR("ESYS-P: Cannot find a good division. Requested workers (%d) do not fit well with TCLB division (%dx%dx%d)\n", workers, nx0, ny0, nz0);
                        return -1;
                }
        }
        output("ESYS-P: Will be running at %dx%dx%d\n", nx, ny, nz);

        if (xcirc) {
                if (nx < 2) {
                        ERROR("ESYS-P can be periodic in X only when there are 2 processes in this direction.\n");
                        return -1;
                }
        }

        
        char fn[STRING_LEN];
        double units[3];
        units[0] = solver->units.alt("1m");
        units[1] = solver->units.alt("1s");
        units[2] = solver->units.alt("1kg");

//	solver->outGlobalFile("ESYS", ".py", fn);
        sprintf(fn, "%s_%s.py", solver->info.outpath, "ESYS");
        output("ESYS-P: config: %s\n", fn);
        if (D_MPI_RANK == 0) {
                FILE * f = fopen(fn, "wt");
                fprintf(f, "from esys.lsm import *\n");
                fprintf(f, "from esys.lsm.util import Vec3, BoundingBox\n");
                fprintf(f, "from esys.lsm.geometry import *\n\n");
                fprintf(f, "%s = LsmMpi(numWorkerProcesses=%d, mpiDimList=[%d,%d,%d])\n", sim.c_str(), nx*ny*nz, nx, ny, nz);
                fprintf(f, "%s.initNeighbourSearch( particleType=\"%s\", gridSpacing=%lg, verletDist=%lg )\n", sim.c_str(), particle_type.c_str(), gridSpacing, verletDist);
                fprintf(f, "%s.setSpatialDomain( BoundingBox(Vec3(%lg,%lg,%lg), Vec3(%lg,%lg,%lg)), circDimList = [%s, False, False])\n",
                        sim.c_str(),
                        0.0, 0.0, 0.0,
                        sx/units[0], sy/units[0], sz/units[0],
                        xcirc ? "True" : "False");
                fprintf(f, "%s.setTimeStepSize(%lg)\n", sim.c_str(), 1.0/units[1]);
                fprintf(f, "%s.setNumTimeSteps(%d)\n", sim.c_str(), Next(solver->iter));
                fprintf(f, "%s.createInteractionGroup(	TCLBForcePrms(name=\"tclb\", acceleration=Vec3(0,-9.81,0), fluidDensity=1.0, fluidHeight=0) )\n", sim.c_str());
                fprintf(f, "output_prefix=\"%s_%s\"\n", solver->info.outpath, "ESYS");
                fprintf(f, "def output_path(x):\n\treturn output_prefix + x\n");
                fprintf(f, "%s\n", node.child_value());
                fprintf(f, "%s.run()\n", sim.c_str());
                fflush(f);
                fclose(f);
        }
        MPI_Barrier(MPI_COMM_WORLD);

        char * args[] = {fn,NULL};
        
        solver->lattice->RFI.Start("esysparticle",args, units);
	return 0;
}


// Register the handler (basing on xmlname) in the Handler Factory
template class HandlerFactory::Register< GenericAsk< acRemoteForceInterface > >;
