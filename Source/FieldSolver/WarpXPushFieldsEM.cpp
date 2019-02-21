
#include <cmath>
#include <limits>

#include <WarpX.H>
#include <WarpXConst.H>
#include <WarpX_f.H>
#ifdef WARPX_USE_PY
#include <WarpX_py.H>
#endif

#ifdef BL_USE_SENSEI_INSITU
#include <AMReX_AmrMeshInSituBridge.H>
#endif

using namespace amrex;

void
WarpX::EvolveB (Real dt)
{
    for (int lev = 0; lev <= finest_level; ++lev) {
        EvolveB(lev, dt);
    }
}

void
WarpX::EvolveB (int lev, Real dt)
{
    BL_PROFILE("WarpX::EvolveB()");
    EvolveB(lev, PatchType::fine, dt);
    if (lev > 0)
    {
        EvolveB(lev, PatchType::coarse, dt);
    }
}

void
WarpX::EvolveB (int lev, PatchType patch_type, amrex::Real dt)
{
    const int patch_level = (patch_type == PatchType::fine) ? lev : lev-1;
    const std::array<Real,3>& dx = WarpX::CellSize(patch_level);
    const std::array<Real,3> dtsdx {dt/dx[0], dt/dx[1], dt/dx[2]};

    MultiFab *Ex, *Ey, *Ez, *Bx, *By, *Bz;
    if (patch_type == PatchType::fine)
    {
        Ex = Efield_fp[lev][0].get();
        Ey = Efield_fp[lev][1].get();
        Ez = Efield_fp[lev][2].get();
        Bx = Bfield_fp[lev][0].get();
        By = Bfield_fp[lev][1].get();
        Bz = Bfield_fp[lev][2].get();
    }
    else
    {
        Ex = Efield_cp[lev][0].get();
        Ey = Efield_cp[lev][1].get();
        Ez = Efield_cp[lev][2].get();
        Bx = Bfield_cp[lev][0].get();
        By = Bfield_cp[lev][1].get();
        Bz = Bfield_cp[lev][2].get();
    }

    MultiFab* cost = costs[lev].get();
    const IntVect& rr = (lev > 0) ? refRatio(lev-1) : IntVect::TheUnitVector();

    // Loop through the grids, and over the tiles within each grid
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for ( MFIter mfi(*Bx, TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Real wt = amrex::second();

        const Box& tbx  = mfi.tilebox(Bx_nodal_flag);
        const Box& tby  = mfi.tilebox(By_nodal_flag);
        const Box& tbz  = mfi.tilebox(Bz_nodal_flag);

        // Call picsar routine for each tile
        warpx_push_bvec(
		      tbx.loVect(), tbx.hiVect(),
		      tby.loVect(), tby.hiVect(),
		      tbz.loVect(), tbz.hiVect(),
		      BL_TO_FORTRAN_3D((*Ex)[mfi]),
		      BL_TO_FORTRAN_3D((*Ey)[mfi]),
		      BL_TO_FORTRAN_3D((*Ez)[mfi]),
		      BL_TO_FORTRAN_3D((*Bx)[mfi]),
		      BL_TO_FORTRAN_3D((*By)[mfi]),
		      BL_TO_FORTRAN_3D((*Bz)[mfi]),
		      &dtsdx[0], &dtsdx[1], &dtsdx[2],
		      &WarpX::maxwell_fdtd_solver_id);

        if (cost) {
            Box cbx = mfi.tilebox(IntVect{AMREX_D_DECL(0,0,0)});
            if (patch_type == PatchType::coarse) cbx.refine(rr);
            wt = (amrex::second() - wt) / cbx.d_numPts();\
            FArrayBox* costfab = cost->fabPtr(mfi);
            AMREX_LAUNCH_HOST_DEVICE_LAMBDA ( cbx, work_box,
            {
                costfab->plus(wt, work_box);
            });
        }
    }

    if (do_pml && pml[lev]->ok())
    {
        const auto& pml_B = (patch_type == PatchType::fine) ? pml[lev]->GetB_fp() : pml[lev]->GetB_cp();
        const auto& pml_E = (patch_type == PatchType::fine) ? pml[lev]->GetE_fp() : pml[lev]->GetE_cp();

#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for ( MFIter mfi(*pml_B[0], TilingIfNotGPU()); mfi.isValid(); ++mfi )
        {
            const Box& tbx  = mfi.tilebox(Bx_nodal_flag);
            const Box& tby  = mfi.tilebox(By_nodal_flag);
            const Box& tbz  = mfi.tilebox(Bz_nodal_flag);

            WRPX_PUSH_PML_BVEC(
			     tbx.loVect(), tbx.hiVect(),
			     tby.loVect(), tby.hiVect(),
			     tbz.loVect(), tbz.hiVect(),
			     BL_TO_FORTRAN_3D((*pml_E[0])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_E[1])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_E[2])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_B[0])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_B[1])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_B[2])[mfi]),
			     &dtsdx[0], &dtsdx[1], &dtsdx[2],
			     &WarpX::maxwell_fdtd_solver_id);
        }
    }
}

void
WarpX::EvolveE (Real dt)
{
    for (int lev = 0; lev <= finest_level; ++lev)
    {
        EvolveE(lev, dt);
    }
}

void
WarpX::EvolveE (int lev, Real dt)
{
    BL_PROFILE("WarpX::EvolveE()");
    EvolveE(lev, PatchType::fine, dt);
    if (lev > 0)
    {
        EvolveE(lev, PatchType::coarse, dt);
    }
}

void
WarpX::EvolveE (int lev, PatchType patch_type, amrex::Real dt)
{
    const Real mu_c2_dt = (PhysConst::mu0*PhysConst::c*PhysConst::c) * dt;
    const Real c2dt = (PhysConst::c*PhysConst::c) * dt;

    int patch_level = (patch_type == PatchType::fine) ? lev : lev-1;
    const std::array<Real,3>& dx = WarpX::CellSize(patch_level);
    const std::array<Real,3> dtsdx_c2 {c2dt/dx[0], c2dt/dx[1], c2dt/dx[2]};

    MultiFab *Ex, *Ey, *Ez, *Bx, *By, *Bz, *jx, *jy, *jz, *F;
    if (patch_type == PatchType::fine)
    {
        Ex = Efield_fp[lev][0].get();
        Ey = Efield_fp[lev][1].get();
        Ez = Efield_fp[lev][2].get();
        Bx = Bfield_fp[lev][0].get();
        By = Bfield_fp[lev][1].get();
        Bz = Bfield_fp[lev][2].get();
        jx = current_fp[lev][0].get();
        jy = current_fp[lev][1].get();
        jz = current_fp[lev][2].get();
        F  = F_fp[lev].get();
    }
    else if (patch_type == PatchType::coarse)
    {
        Ex = Efield_cp[lev][0].get();
        Ey = Efield_cp[lev][1].get();
        Ez = Efield_cp[lev][2].get();
        Bx = Bfield_cp[lev][0].get();
        By = Bfield_cp[lev][1].get();
        Bz = Bfield_cp[lev][2].get();
        jx = current_cp[lev][0].get();
        jy = current_cp[lev][1].get();
        jz = current_cp[lev][2].get();
        F  = F_cp[lev].get();
    }

    MultiFab* cost = costs[lev].get();
    const IntVect& rr = (lev > 0) ? refRatio(lev-1) : IntVect::TheUnitVector();

    // Loop through the grids, and over the tiles within each grid
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for ( MFIter mfi(*Ex, TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Real wt = amrex::second();

        const Box& tex  = mfi.tilebox(Ex_nodal_flag);
        const Box& tey  = mfi.tilebox(Ey_nodal_flag);
        const Box& tez  = mfi.tilebox(Ez_nodal_flag);

        // Call picsar routine for each tile
        warpx_push_evec(
		      tex.loVect(), tex.hiVect(),
		      tey.loVect(), tey.hiVect(),
		      tez.loVect(), tez.hiVect(),
		      BL_TO_FORTRAN_3D((*Ex)[mfi]),
		      BL_TO_FORTRAN_3D((*Ey)[mfi]),
		      BL_TO_FORTRAN_3D((*Ez)[mfi]),
		      BL_TO_FORTRAN_3D((*Bx)[mfi]),
		      BL_TO_FORTRAN_3D((*By)[mfi]),
		      BL_TO_FORTRAN_3D((*Bz)[mfi]),
		      BL_TO_FORTRAN_3D((*jx)[mfi]),
		      BL_TO_FORTRAN_3D((*jy)[mfi]),
		      BL_TO_FORTRAN_3D((*jz)[mfi]),
		      &mu_c2_dt,
		      &dtsdx_c2[0], &dtsdx_c2[1], &dtsdx_c2[2]);

        if (F)
        {
            warpx_push_evec_f(
			  tex.loVect(), tex.hiVect(),
			  tey.loVect(), tey.hiVect(),
			  tez.loVect(), tez.hiVect(),
			  BL_TO_FORTRAN_3D((*Ex)[mfi]),
			  BL_TO_FORTRAN_3D((*Ey)[mfi]),
			  BL_TO_FORTRAN_3D((*Ez)[mfi]),
			  BL_TO_FORTRAN_3D((*F)[mfi]),
			  &dtsdx_c2[0], &dtsdx_c2[1], &dtsdx_c2[2],
			  &WarpX::maxwell_fdtd_solver_id);
        }

        if (cost) {
            Box cbx = mfi.tilebox(IntVect{AMREX_D_DECL(0,0,0)});
            if (patch_type == PatchType::coarse) cbx.refine(rr);
            wt = (amrex::second() - wt) / cbx.d_numPts();
            FArrayBox* costfab = cost->fabPtr(mfi);
            AMREX_LAUNCH_HOST_DEVICE_LAMBDA ( cbx, work_box,
            {
                costfab->plus(wt, work_box);
            });
        }
    }

    if (do_pml && pml[lev]->ok())
    {
        if (F) pml[lev]->ExchangeF(patch_type, F);

        const auto& pml_B = (patch_type == PatchType::fine) ? pml[lev]->GetB_fp() : pml[lev]->GetB_cp();
        const auto& pml_E = (patch_type == PatchType::fine) ? pml[lev]->GetE_fp() : pml[lev]->GetE_cp();
        const auto& pml_F = (patch_type == PatchType::fine) ? pml[lev]->GetF_fp() : pml[lev]->GetF_cp();
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for ( MFIter mfi(*pml_E[0], TilingIfNotGPU()); mfi.isValid(); ++mfi )
        {
            const Box& tex  = mfi.tilebox(Ex_nodal_flag);
            const Box& tey  = mfi.tilebox(Ey_nodal_flag);
            const Box& tez  = mfi.tilebox(Ez_nodal_flag);

            WRPX_PUSH_PML_EVEC(
			     tex.loVect(), tex.hiVect(),
			     tey.loVect(), tey.hiVect(),
			     tez.loVect(), tez.hiVect(),
			     BL_TO_FORTRAN_3D((*pml_E[0])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_E[1])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_E[2])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_B[0])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_B[1])[mfi]),
			     BL_TO_FORTRAN_3D((*pml_B[2])[mfi]),
			     &dtsdx_c2[0], &dtsdx_c2[1], &dtsdx_c2[2]);

            if (pml_F)
            {
                WRPX_PUSH_PML_EVEC_F(
				   tex.loVect(), tex.hiVect(),
				   tey.loVect(), tey.hiVect(),
				   tez.loVect(), tez.hiVect(),
				   BL_TO_FORTRAN_3D((*pml_E[0])[mfi]),
				   BL_TO_FORTRAN_3D((*pml_E[1])[mfi]),
				   BL_TO_FORTRAN_3D((*pml_E[2])[mfi]),
				   BL_TO_FORTRAN_3D((*pml_F   )[mfi]),
				   &dtsdx_c2[0], &dtsdx_c2[1], &dtsdx_c2[2],
				   &WarpX::maxwell_fdtd_solver_id);
            }
        }
    }
}

void
WarpX::EvolveF (Real dt, DtType dt_type)
{
    if (!do_dive_cleaning) return;

    for (int lev = 0; lev <= finest_level; ++lev)
    {
        EvolveF(lev, dt, dt_type);
    }
}

void
WarpX::EvolveF (int lev, Real dt, DtType dt_type)
{
    if (!do_dive_cleaning) return;

    EvolveF(lev, PatchType::fine, dt, dt_type);
    if (lev > 0) EvolveF(lev, PatchType::coarse, dt, dt_type);
}

void
WarpX::EvolveF (int lev, PatchType patch_type, Real dt, DtType dt_type)
{
    if (!do_dive_cleaning) return;

    BL_PROFILE("WarpX::EvolveF()");

    static constexpr Real mu_c2 = PhysConst::mu0*PhysConst::c*PhysConst::c;

    int patch_level = (patch_type == PatchType::fine) ? lev : lev-1;
    const auto& dx = WarpX::CellSize(patch_level);
    const std::array<Real,3> dtsdx {dt/dx[0], dt/dx[1], dt/dx[2]};

    MultiFab *Ex, *Ey, *Ez, *rho, *F;
    if (patch_type == PatchType::fine)
    {
        Ex = Efield_fp[lev][0].get();
        Ey = Efield_fp[lev][1].get();
        Ez = Efield_fp[lev][2].get();
        rho = rho_fp[lev].get();
        F = F_fp[lev].get();
    }
    else
    {
        Ex = Efield_cp[lev][0].get();
        Ey = Efield_cp[lev][1].get();
        Ez = Efield_cp[lev][2].get();
        rho = rho_cp[lev].get();
        F = F_cp[lev].get();
    }

    const int rhocomp = (dt_type == DtType::FirstHalf) ? 0 : 1;

    MultiFab src(rho->boxArray(), rho->DistributionMap(), 1, 0);
    ComputeDivE(src, 0, {Ex,Ey,Ez}, dx);
    MultiFab::Saxpy(src, -mu_c2, *rho, rhocomp, 0, 1, 0);
    MultiFab::Saxpy(*F, dt, src, 0, 0, 1, 0);

    if (do_pml && pml[lev]->ok())
    {
        const auto& pml_F = (patch_type == PatchType::fine) ? pml[lev]->GetF_fp() : pml[lev]->GetF_cp();
        const auto& pml_E = (patch_type == PatchType::fine) ? pml[lev]->GetE_fp() : pml[lev]->GetE_cp();

#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for ( MFIter mfi(*pml_F, TilingIfNotGPU()); mfi.isValid(); ++mfi )
        {
            const Box& bx = mfi.tilebox();
            WRPX_PUSH_PML_F(bx.loVect(), bx.hiVect(),
			  BL_TO_FORTRAN_ANYD((*pml_F   )[mfi]),
			  BL_TO_FORTRAN_ANYD((*pml_E[0])[mfi]),
			  BL_TO_FORTRAN_ANYD((*pml_E[1])[mfi]),
			  BL_TO_FORTRAN_ANYD((*pml_E[2])[mfi]),
			  &dtsdx[0], &dtsdx[1], &dtsdx[2]);
        }
    }
}

