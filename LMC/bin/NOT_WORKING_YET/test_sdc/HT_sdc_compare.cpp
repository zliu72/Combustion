
//
// "Divu_Type" means S, where divergence U = S
// "Dsdt_Type" means pd S/pd t, where S is as above
// "Ydot_Type" means -omega_l/rho, i.e., the mass rate of decrease of species l due
//             to kinetics divided by rho
//
#include <winstd.H>

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <cfloat>
#include <fstream>
#include <vector>

#include <Geometry.H>
#include <BoxDomain.H>
#include <ParmParse.H>
#include <ErrorList.H>
#include <HeatTransfer.H>
#include <HEATTRANSFER_F.H>
#include <DIFFUSION_F.H>
#include <MultiGrid.H>
#include <ArrayLim.H>
#include <SPACE.H>
#include <Interpolater.H>
#include <ccse-mpi.H>
#include <PArray.H>
#include <PROB_F.H>

#if defined(BL_USE_NEWMECH) || defined(BL_USE_VELOCITY)
#include <DataServices.H>
#include <AmrData.H>
#endif

#ifndef NDEBUG
void inspectFAB (FArrayBox& unfab);
void inspectFAB (MultiFab& unfab);
void inspectFAB (MultiFab& unfab, int dummy, int comp);
void inspectFAB (MultiFab* unfab, int i);
#endif

#include <PROB_F.H>
#include <NAVIERSTOKES_F.H>
#include <VISCOPERATOR_F.H>
#include <DERIVE_F.H>

#define DEF_LIMITS(fab,fabdat,fablo,fabhi)   \
const int* fablo = (fab).loVect();           \
const int* fabhi = (fab).hiVect();           \
Real* fabdat = (fab).dataPtr();

#define DEF_CLIMITS(fab,fabdat,fablo,fabhi)  \
const int* fablo = (fab).loVect();           \
const int* fabhi = (fab).hiVect();           \
const Real* fabdat = (fab).dataPtr();

#define DEF_CLIMITSCOMP(fab,fabdat,fablo,fabhi,comp)  \
const int* fablo = (fab).loVect();                    \
const int* fabhi = (fab).hiVect();                    \
const Real* fabdat = (fab).dataPtr(comp);

#define GEOM_GROW   1
#define PRESS_GROW  1
#define DIVU_GROW   1
#define DSDT_GROW   1
#define bogus_value 1.e20
#define DQRAD_GROW  1
#define YDOT_GROW   1
const int LinOp_grow = 1;

//
// Initialization of static members.
//
int       HeatTransfer::num_divu_iters        = 1;
int       HeatTransfer::init_once_done        = 0;
int       HeatTransfer::do_OT_radiation       = 0;
int       HeatTransfer::do_heat_sink          = 0;
int       HeatTransfer::RhoH                  = -1;
int       HeatTransfer::do_diffuse_sync       = 1;
int       HeatTransfer::do_reflux_visc        = 1;
int       HeatTransfer::dpdt_option           = 2;
int       HeatTransfer::Ydot_Type             = -1;
int       HeatTransfer::FuncCount_Type        = -1;
int       HeatTransfer::divu_ceiling          = 0;
Real      HeatTransfer::divu_dt_factor        = .5;
Real      HeatTransfer::min_rho_divu_ceiling  = -1.e20;
int       HeatTransfer::have_trac                 = 0;
int       HeatTransfer::have_rhort                = 0;
int       HeatTransfer::Trac                      = -1;
int       HeatTransfer::RhoRT                     = -1;
int       HeatTransfer::first_spec                = -1;
int       HeatTransfer::last_spec                 = -2;
int       HeatTransfer::nspecies                  = 0;
int       HeatTransfer::floor_species             = 0;
int       HeatTransfer::do_set_rho_to_species_sum = 1;
Real      HeatTransfer::rgas                      = -1.0;
Real      HeatTransfer::prandtl                   = .7;
Real      HeatTransfer::schmidt                   = .7;
Real      HeatTransfer::constant_mu_val           = -1;
Real      HeatTransfer::constant_rhoD_val         = -1;
Real      HeatTransfer::constant_lambda_val       = -1;
int       HeatTransfer::unity_Le                  = 1;
Real      HeatTransfer::htt_tempmin               = 298.0;
Real      HeatTransfer::htt_tempmax               = 40000.;
int       HeatTransfer::siegel_test               = 0;
int       HeatTransfer::zeroBndryVisc             = 0;
ChemDriver* HeatTransfer::chemSolve               = 0;
int       HeatTransfer::do_add_nonunityLe_corr_to_rhoh_adv_flux = 1;
int       HeatTransfer::do_check_divudt           = 1;
int       HeatTransfer::hack_nochem               = 1;
int       HeatTransfer::hack_nospecdiff           = 0;
int       HeatTransfer::hack_noavgdivu            = 0;
Real      HeatTransfer::trac_diff_coef            = 0.0;
Real      HeatTransfer::P1atm_MKS                 = -1.0;
std::string   HeatTransfer::turbFile                  ="";
ChemDriver::Chem_Evolve HeatTransfer::chem_integrator = ChemDriver::CKD_Vode;
std::map<std::string, Array<std::string> > HeatTransfer::auxDiag_names;
bool      HeatTransfer::plot_reactions            = false;
bool      HeatTransfer::plot_consumption          = true;
bool      HeatTransfer::plot_heat_release         = true;
int       HeatTransfer::do_mcdd                   = 0;
int       HeatTransfer::mcdd_NitersMAX            = 100;
Real      HeatTransfer::mcdd_relaxFactor          = 1.0;
Real      HeatTransfer::mcdd_relaxFactor1         = 1.0;
Real      HeatTransfer::mcdd_rtol                 = 1.e-8;
int       HeatTransfer::mcdd_presmooth            = 3;
int       HeatTransfer::mcdd_postsmooth           = 3;
Real      HeatTransfer::mcdd_cfRelaxFactor        = 1.0;
bool      HeatTransfer::do_rk_diffusion           = false;
bool      HeatTransfer::rk_mixture_averaged       = false;
Real      HeatTransfer::rk_time_step_multiplier   = 0.5;

Real      HeatTransfer::new_T_threshold           = -1;  // On new AMR level, max change in lower bound for T, not used if <=0

///////////////////
// SDC variables //
///////////////////
bool HeatTransfer::use_sdc    = true;
int  HeatTransfer::sdc_iters  = 1;
// 1 = Lobatto
// 2 = radau   -- need to think about how to do this more
int  HeatTransfer::quadrature = 1;

 MultiFab*   sdcForce;
 const int   nGrowForce = 1;
 PArray<MultiFab>  DofS;
// holds the additional terms in the RhoH equation that arise from
//    having a Non-unity Lewis number (NULN)
 PArray<MultiFab>  RhoH_NULN_terms;
 MultiFab**        reflux_terms; 
 // inhert aofs from NavierStokes ::  MultiFab* aofs; no ghost cells
 PArray<MultiFab>  I_R;
 PArray<MultiFab>  I_AD;

// maybe need to move actual values into init_once()?
 // radau values
//  const int n_I = 3;
//  const int n_diffusion = 4;
//  const Real sdc_dt_fac[] = {(1.0/3.0),(2.0/3.0)};
 // lobatto values
 const int n_I = 1;
 const int n_diffusion = 3;
 const Real sdc_dt_fac[] = {0,1};
///////////////////


static int  max_grid_size_chem   = 16;
static bool do_not_use_funccount = false;
static bool do_active_control    = false;
static Real crse_dt = -1;

#ifdef BL_USE_FLOAT
#  define Real_MIN FLT_MIN
#  define Real_MAX FLT_MAX
#else
#  define Real_MIN DBL_MIN
#  define Real_MAX DBL_MAX
#endif

static
void
FabMinMax (FArrayBox& fab,
           const Box& box,
           Real       fmin,
           Real       fmax,
           int        sComp,
           int        nComp)
{
    BL_ASSERT(fab.box().contains(box));
    BL_ASSERT(sComp + nComp <= fab.nComp());

    const int* lo     = box.loVect();
    const int* hi     = box.hiVect();
    Real*      fabdat = fab.dataPtr(sComp);
    
    FORT_FABMINMAX(lo, hi,
                   fabdat, ARLIM(fab.loVect()), ARLIM(fab.hiVect()),
                   &fmin, &fmax, &nComp);
}

HeatTransfer::FPLoc 
HeatTransfer::fpi_phys_loc (int p_bc)
{
    //
    // Location of data that FillPatchIterator returns at physical boundaries
    //
    if (p_bc == EXT_DIR || p_bc == HOEXTRAP || p_bc == FOEXTRAP)
    {
        return HT_Edge;
    }
    return HT_Center;
}
    
void
HeatTransfer::center_to_edge_fancy (const FArrayBox& cfab,
                                    FArrayBox&       efab,
                                    const Box&       ccBox,
                                    int              sComp,
                                    int              dComp,
                                    int              nComp,
                                    const Box&       domain,
                                    const FPLoc&     bc_lo,
                                    const FPLoc&     bc_hi)
{
    const Box&      ebox = efab.box();
    const IndexType ixt  = ebox.ixType();

    BL_ASSERT(!(ixt.cellCentered()) && !(ixt.nodeCentered()));

    int dir = -1;
    for (int d = 0; d < BL_SPACEDIM; d++)
        if (ixt.test(d))
            dir = d;

    BL_ASSERT(BoxLib::grow(ccBox,-BoxLib::BASISV(dir)).contains(BoxLib::enclosedCells(ebox)));
    BL_ASSERT(sComp+nComp <= cfab.nComp() && dComp+nComp <= efab.nComp());
    //
    // Exclude unnecessary cc->ec calcs
    //
    Box ccVBox = ccBox;
    if (bc_lo!=HT_Center)
        ccVBox.setSmall(dir,std::max(domain.smallEnd(dir),ccVBox.smallEnd(dir)));
    if (bc_hi!=HT_Center)
        ccVBox.setBig(dir,std::min(domain.bigEnd(dir),ccVBox.bigEnd(dir)));
    //
    // Shift cell-centered data to edges
    //
    const int isharm = def_harm_avg_cen2edge?1:0;
    FORT_CEN2EDG(ccVBox.loVect(),ccVBox.hiVect(),
                 ARLIM(cfab.loVect()),ARLIM(cfab.hiVect()),cfab.dataPtr(sComp),
                 ARLIM(efab.loVect()),ARLIM(efab.hiVect()),efab.dataPtr(dComp),
                 &nComp, &dir, &isharm);
    //
    // Fix boundary...i.e. fill-patched data in cfab REALLY lives on edges
    //
    if ( !(domain.contains(ccBox)) )
    {
        if (bc_lo==HT_Edge)
        {
            BoxList gCells = BoxLib::boxDiff(ccBox,domain);
            if (gCells.ok())
            {
                const int inc = +1;
                FArrayBox ovlpFab;
                for (BoxList::iterator bli = gCells.begin();
                     bli != gCells.end();
                     ++bli)
                {
                    if (bc_lo == HT_Edge)
                    {
                        ovlpFab.resize(*bli,nComp);
                        ovlpFab.copy(cfab,sComp,0,nComp);
                        ovlpFab.shiftHalf(dir,inc);
                        efab.copy(ovlpFab,0,dComp,nComp);
                    }
                }
            }
        }
        if (bc_hi==HT_Edge)
        {
            BoxList gCells = BoxLib::boxDiff(ccBox,domain);
            if (gCells.ok())
            {
                const int inc = -1;
                FArrayBox ovlpFab;
                for (BoxList::iterator bli = gCells.begin();
                     bli != gCells.end();
                     ++bli)
                {
                    if (bc_hi == HT_Edge)
                    {
                        ovlpFab.resize(*bli,nComp);
                        ovlpFab.copy(cfab,sComp,0,nComp);
                        ovlpFab.shiftHalf(dir,inc);
                        efab.copy(ovlpFab,0,dComp,nComp);
                    }
                }
            }
        }
    }
}    

void
HeatTransfer::variableCleanUp ()
{
    NavierStokes::variableCleanUp();

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "Deleting chemSolver in variableCleanUp ... " << std::flush;

    delete chemSolve;

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "done\n";

    chemSolve = 0;
}

void
HeatTransfer::read_params ()
{
    //
    // Read parameters from input file and command line.
    //
    NavierStokes::read_params();

    ParmParse pp("ns");

    pp.query("do_diffuse_sync",do_diffuse_sync);
    BL_ASSERT(do_diffuse_sync == 0 || do_diffuse_sync == 1);
    pp.query("do_reflux_visc",do_reflux_visc);
    BL_ASSERT(do_reflux_visc == 0 || do_reflux_visc == 1);
    pp.query("dpdt_option",dpdt_option);
    BL_ASSERT(dpdt_option >= 0 && dpdt_option <= 2);
    pp.query("max_grid_size_chem",max_grid_size_chem);
    BL_ASSERT(max_grid_size_chem > 0);
    pp.query("do_active_control",do_active_control);

    pp.query("v",verbose);

    pp.query("divu_ceiling",divu_ceiling);
    BL_ASSERT(divu_ceiling >= 0 && divu_ceiling <= 3);
    pp.query("divu_dt_factor",divu_dt_factor);
    BL_ASSERT(divu_dt_factor>0 && divu_dt_factor <= 1.0);
    pp.query("min_rho_divu_ceiling",min_rho_divu_ceiling);
    if (divu_ceiling) BL_ASSERT(min_rho_divu_ceiling >= 0.0);

    pp.query("htt_tempmin",htt_tempmin);
    pp.query("htt_tempmax",htt_tempmax);

    pp.query("floor_species",floor_species);
    BL_ASSERT(floor_species == 0 || floor_species == 1);

    pp.query("do_set_rho_to_species_sum",do_set_rho_to_species_sum);

    pp.query("num_divu_iters",num_divu_iters);

    pp.query("do_not_use_funccount",do_not_use_funccount);

    pp.query("schmidt",schmidt);
    pp.query("prandtl",prandtl);
    pp.query("unity_Le",unity_Le);
    unity_Le = unity_Le ? 1 : 0;
    if (unity_Le)
    {
	schmidt = prandtl;
        if (ParallelDescriptor::IOProcessor())
            std::cout << "HeatTransfer::read_params: Le=1, setting Sc = Pr" << std::endl;
    }

    pp.query("constant_mu_val",constant_mu_val);
    pp.query("constant_rhoD_val",constant_rhoD_val);
    pp.query("constant_lambda_val",constant_lambda_val);
    if (constant_mu_val != -1)
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "HeatTransfer::read_params: using constant_mu_val = " 
                      << constant_mu_val << std::endl;
    }
    if (constant_rhoD_val != -1)
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "HeatTransfer::read_params: using constant_rhoD_val = " 
                      << constant_rhoD_val << std::endl;
    }
    if (constant_lambda_val != -1)
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "HeatTransfer::read_params: using constant_lambda_val = " 
                      << constant_lambda_val << std::endl;
    }

    pp.query("do_add_nonunityLe_corr_to_rhoh_adv_flux",
             do_add_nonunityLe_corr_to_rhoh_adv_flux);
    pp.query("hack_nochem",hack_nochem);
    pp.query("hack_nospecdiff",hack_nospecdiff);
    pp.query("hack_noavgdivu",hack_noavgdivu);
    pp.query("do_check_divudt",do_check_divudt);

    pp.query("do_OT_radiation",do_OT_radiation);
    do_OT_radiation = (do_OT_radiation ? 1 : 0);
    pp.query("do_heat_sink",do_heat_sink);
    do_heat_sink = (do_heat_sink ? 1 : 0);

    int use_chemeq2 = 0; pp.query("use_chemeq2",use_chemeq2);
    chem_integrator = (use_chemeq2 ? ChemDriver::CKD_ChemEQ2 : ChemDriver::CKD_Vode );

    std::string tranfile=""; pp.query("tranfile",tranfile);
    chemSolve = new ChemDriver(tranfile);

    pp.query("turbFile",turbFile);

    pp.query("siegel_test",siegel_test);
    pp.query("zeroBndryVisc",zeroBndryVisc);
    //
    // Set variability/visc for velocities.
    //
    if (variable_vel_visc != 1)
      BoxLib::Error("HeatTransfer::read_params() -- must use variable viscosity");
    //
    // Set variability/visc for tracer
    //
    if (variable_scal_diff != 1)
      BoxLib::Error("HeatTransfer::read_params() -- must use variable scalar diffusivity");
    //
    // Read in scalar value and use it as tracer.
    //
    pp.query("scal_diff_coefs",trac_diff_coef);

    for (int i = 0; i < visc_coef.size(); i++)
        visc_coef[i] = bogus_value;

    pp.query("do_mcdd",do_mcdd);
    pp.query("mcdd_NitersMAX",mcdd_NitersMAX);
    pp.query("mcdd_relaxFactor",mcdd_relaxFactor);
    pp.query("mcdd_relaxFactor1",mcdd_relaxFactor1);
    pp.query("mcdd_rtol",mcdd_rtol);
    pp.query("mcdd_presmooth",mcdd_presmooth);
    pp.query("mcdd_postsmooth",mcdd_postsmooth);
    pp.query("mcdd_cfRelaxFactor",mcdd_cfRelaxFactor);

    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "\nDumping ParmParse table:\n \n";
        ParmParse::dumpTable(std::cout);
        std::cout << "\n... done dumping ParmParse table.\n" << std::endl;
    }
}

HeatTransfer::HeatTransfer ()
    : MCDDOp(getChemSolve())
{
    if (!init_once_done)
        init_once();

    if (!do_temp)
        BoxLib::Abort("do_temp MUST be true");

    if (!have_divu)
        BoxLib::Abort("have_divu MUST be true");

    if (!have_dsdt)
        BoxLib::Abort("have_dsdt MUST be true");

    EdgeState              = 0;
    SpecDiffusionFluxn     = 0;
    SpecDiffusionFluxnp1   = 0;
    FillPatchedOldState_ok = true;
}

HeatTransfer::HeatTransfer (Amr&            papa,
                            int             lev,
                            const Geometry& level_geom,
                            const BoxArray& bl,
                            Real            time)
    :
    NavierStokes(papa,lev,level_geom,bl,time),
    //
    // Make room for all components except velocities in aux_boundary_data_old.
    //
    aux_boundary_data_old(bl,Godunov::hypgrow(),desc_lst[State_Type].nComp()-BL_SPACEDIM,level_geom),
    //
    // Only save Density & RhoH in aux_boundary_data_new in components 0 & 1.
    //
    aux_boundary_data_new(bl,LinOp_grow,2,level_geom),
    FillPatchedOldState_ok(true),
    MCDDOp(getChemSolve())
{
    if (!init_once_done)
        init_once();

    if (!do_temp)
        BoxLib::Abort("do_temp MUST be true");

    if (!have_divu)
        BoxLib::Abort("have_divu MUST be true");

    if (!have_dsdt)
        BoxLib::Abort("have_dsdt MUST be true");

    const int nGrow       = 0;
    const int nEdgeStates = desc_lst[State_Type].nComp();
    diffusion->allocFluxBoxesLevel(EdgeState,nGrow,nEdgeStates);
    if (nspecies>0 && !unity_Le)
    {
	diffusion->allocFluxBoxesLevel(SpecDiffusionFluxn,  nGrow,nspecies);
	diffusion->allocFluxBoxesLevel(SpecDiffusionFluxnp1,nGrow,nspecies);
	spec_diffusion_flux_computed.resize(nspecies,HT_None);
    }

    for (std::map<std::string,Array<std::string> >::iterator it=auxDiag_names.begin();
         it!=auxDiag_names.end(); ++it)
    {
        auxDiag[it->first] = new MultiFab(grids,it->second.size(),0);
        auxDiag[it->first]->setVal(0);
    }

    if (do_mcdd)
        MCDDOp.define(grids,Domain(),crse_ratio);
}

HeatTransfer::~HeatTransfer ()
{
    diffusion->removeFluxBoxesLevel(EdgeState);
    if (nspecies>0 && !unity_Le)    
    {
	diffusion->removeFluxBoxesLevel(SpecDiffusionFluxn);    
	diffusion->removeFluxBoxesLevel(SpecDiffusionFluxnp1);    
    }

    for (std::map<std::string,MultiFab*>::iterator it=auxDiag.begin();
         it!=auxDiag.end(); ++it)
    {
        delete it->second;
    }
}

void
HeatTransfer::init_once ()
{
    //
    // Computes the static variables unique to HeatTransfer.
    // Check that (some) things are set up correctly.
    //
    int dummy_State_Type;

    int have_temp = isStateVariable("temp", dummy_State_Type, Temp);

    have_temp = have_temp && State_Type == dummy_State_Type;
    have_temp = have_temp && isStateVariable("rhoh", dummy_State_Type, RhoH);
    have_temp = have_temp && State_Type == dummy_State_Type;

    have_trac = isStateVariable("tracer", dummy_State_Type, Trac);
    have_trac = have_trac && State_Type == dummy_State_Type;

    have_rhort = isStateVariable("RhoRT", dummy_State_Type, RhoRT);
    have_rhort = have_rhort && State_Type == dummy_State_Type;

    if (!have_temp)
        BoxLib::Abort("HeatTransfer::init_once(): RhoH & Temp must both be the state");
    
    if (!have_rhort && ParallelDescriptor::IOProcessor())
        BoxLib::Warning("HeatTransfer::init_once(): RhoRT being stored in the Tracer slot");
    
    if (Temp < RhoH)
        BoxLib::Abort("HeatTransfer::init_once(): must have RhoH < Temp");
    //
    // Temperature must be non-conservative, rho*h must be conservative.
    //
    if (advectionType[Temp] == Conservative)
        BoxLib::Abort("HeatTransfer::init_once(): Temp must be non-conservative");

    if (advectionType[RhoH] != Conservative)
        BoxLib::Abort("HeatTransfer::init_once(): RhoH must be conservative");
    //
    // Species checks.
    //
    BL_ASSERT(Temp > RhoH && RhoH > Density);
    //
    // Here we want to count relative to Density instead of relative
    // to RhoH, so we can put RhoH after the species instead of before.  
    // This logic should work in both cases.
    //
    first_spec =  Density + 1;
    last_spec  = first_spec + getChemSolve().numSpecies() - 1;
    
    for (int i = first_spec; i <= last_spec; i++)
        if (advectionType[i] != Conservative)
            BoxLib::Error("HeatTransfer::init_once: species must be conservative");
    
    int diffuse_spec = is_diffusive[first_spec];
    for (int i = first_spec+1; i <= last_spec; i++)
        if (is_diffusive[i] != diffuse_spec)
            BoxLib::Error("HeatTransfer::init_once: Le != 1; diffuse");
    //
    // Load integer pointers into Fortran common, reqd for proper ICs.
    //
    const int density = (int)Density;

    FORT_SET_SCAL_NUMB(&density, &Temp, &Trac, &RhoH, &first_spec, &last_spec);
    //
    // Load constants into Fortran common to compute viscosities, etc.
    //
    const int var_visc = (constant_mu_val     == -1 ? 1 : 0);
    const int var_cond = (constant_lambda_val == -1 ? 1 : 0);
    const int var_diff = (constant_rhoD_val   == -1 ? 1 : 0);
    
    FORT_SET_HT_VISC_COMMON(&var_visc, &constant_mu_val,
                            &var_cond, &constant_lambda_val,
                            &var_diff, &constant_rhoD_val,
                            &prandtl,  &schmidt, &unity_Le);
    //
    // Get universal gas constant from Fortran.
    //
    rgas = getChemSolve().getRuniversal();
    P1atm_MKS = getChemSolve().getP1atm_MKS();

    if (rgas <= 0.0)
    {
        std::cerr << "HeatTransfer::init_once(): bad rgas: " << rgas << '\n';
        BoxLib::Abort();
    }
    if (P1atm_MKS <= 0.0)
    {
        std::cerr << "HeatTransfer::init_once(): bad P1atm_MKS: " << P1atm_MKS << '\n';
        BoxLib::Abort();
    }
    //
    // Chemistry.
    //
    int ydot_good = Ydot_Type >= 0 && Ydot_Type <desc_lst.size()
        && Ydot_Type != Divu_Type
        && Ydot_Type != Dsdt_Type
        && Ydot_Type != State_Type;
    
    if (!ydot_good)
        BoxLib::Error("HeatTransfer::init_once(): need Ydot_Type if do_chemistry");
    
    const StateDescriptor& ydot_cell = desc_lst[Ydot_Type];
    int nydot = ydot_cell.nComp();
    if (nydot < nspecies)
        BoxLib::Error("HeatTransfer::init_once(): Ydot_Type needs nspecies components");
    //
    // Enforce Le = 1, unless !unity_Le
    //
    if (unity_Le && (schmidt != prandtl) )
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "**************** WARNING ***************\n"
                      << "HeatTransfer::init_once() : currently must have"
                      << "equal Schmidt and Prandtl numbers unless !unity_Le.\n"
                      << "Setting Schmidt = Prandtl\n"
                      << "**************** WARNING ***************\n";
    
        schmidt = prandtl;
    }
    //
    // We are done.
    //
    num_state_type = desc_lst.size();

    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "HeatTransfer::init_once(): num_state_type = "
                  << num_state_type << '\n';
    }
    ParmParse pp("ht");

    pp.query ("do_rk_diffusion", do_rk_diffusion);
    pp.query ("rk_mixture_averaged", rk_mixture_averaged);
    pp.query ("rk_time_step_multiplier", rk_time_step_multiplier);

    pp.query("plot_reactions",plot_reactions);
    if (plot_reactions)
    {
        auxDiag_names["REACTIONS"].resize(getChemSolve().numReactions());
        for (int i = 0; i < auxDiag_names["REACTIONS"].size(); ++i)
            auxDiag_names["REACTIONS"][i] = BoxLib::Concatenate("R",i+1);
        if (ParallelDescriptor::IOProcessor())
            std::cout << "***** Make sure to increase amr.regrid_int !!!!!" << std::endl;
    }

    pp.query("plot_consumption",plot_consumption);
    pp.query("plot_auxDiags",plot_consumption); // This is for backward comptibility - FIXME
    if (plot_consumption)
    {
        auxDiag_names["CONSUMPTION"].resize(consumptionName.size());
        for (int j=0; j<consumptionName.size(); ++j)
        {
            auxDiag_names["CONSUMPTION"][j] = consumptionName[j] + "_ConsumptionRate";
        }
    }

    pp.query("plot_heat_release",plot_heat_release);
    if (plot_heat_release)
    {
        auxDiag_names["HEATRELEASE"].resize(1);
        auxDiag_names["HEATRELEASE"][0] = "HeatRelease";
    }
    
    pp.query("new_T_threshold",new_T_threshold);


    pp.query("hack_nospecdiff",hack_nospecdiff);
    /////////////////
    // SDC parameters
    /////////////////
    pp.query("use_sdc",use_sdc);
    pp.query("sdc_iters",sdc_iters);
    pp.query("quadrature",quadrature);

    init_once_done = 1;
}

void
HeatTransfer::restart (Amr&          papa,
                       std::istream& is,
                       bool          bReadSpecial)
{

    NavierStokes::restart(papa,is,bReadSpecial);
    //
    // Make room for all components except velocities in aux_boundary_data_old.
    //
    aux_boundary_data_old.initialize(grids,Godunov::hypgrow(),desc_lst[State_Type].nComp()-BL_SPACEDIM,Geom());
    //
    // Only save Density & RhoH in aux_boundary_data_new in components 0 & 1.
    //
    aux_boundary_data_new.initialize(grids,LinOp_grow,2,Geom());

    FillPatchedOldState_ok = true;

    set_overdetermined_boundary_cells(state[State_Type].curTime());

    BL_ASSERT(EdgeState == 0);
    const int nGrow       = 0;
    const int nEdgeStates = desc_lst[State_Type].nComp();
    diffusion->allocFluxBoxesLevel(EdgeState,nGrow,nEdgeStates);
    
    if (nspecies>0 && !unity_Le)
    {
	BL_ASSERT(SpecDiffusionFluxn == 0);
	BL_ASSERT(SpecDiffusionFluxnp1 == 0);
	diffusion->allocFluxBoxesLevel(SpecDiffusionFluxn,  nGrow,nspecies);
	diffusion->allocFluxBoxesLevel(SpecDiffusionFluxnp1,nGrow,nspecies);
	spec_diffusion_flux_computed.resize(nspecies,HT_None);
    }

    for (std::map<std::string,Array<std::string> >::iterator it=auxDiag_names.begin();
         it!=auxDiag_names.end(); ++it)
    {
        auxDiag[it->first] = new MultiFab(grids,it->second.size(),0);
        auxDiag[it->first]->setVal(0);
    }

    if (do_mcdd)
        MCDDOp.define(grids,Domain(),crse_ratio);
}

Real
HeatTransfer::estTimeStep ()
{
    Real estdt = NavierStokes::estTimeStep();

    if (fixed_dt > 0.0 || !divu_ceiling)
        //
        // The n-s function did the right thing in this case.
        //
        return estdt;

    Real dt, ns_estdt = estdt, divu_dt = 1.0e20;

    const int   n_grow   = 1;
    const Real  cur_time = state[State_Type].curTime();
    const Real* dx       = geom.CellSize();
    MultiFab*   dsdt     = getDsdt(0,cur_time);
    MultiFab*   divu     = getDivCond(0,cur_time);

    FArrayBox area[BL_SPACEDIM], volume;

    for (FillPatchIterator U_fpi(*this,*divu,n_grow,cur_time,State_Type,Xvel,BL_SPACEDIM);
         U_fpi.isValid();
         ++U_fpi)
    {
        const int        i   = U_fpi.index();
        FArrayBox&       U   = U_fpi();
        const FArrayBox& Rho = (*rho_ctime)[i];
        const int*       lo  = grids[i].loVect();
        const int*       hi  = grids[i].hiVect();

        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            geom.GetFaceArea(area[dir],grids,i,dir,GEOM_GROW);
        }
        geom.GetVolume(volume, grids, i, GEOM_GROW);

        DEF_CLIMITS((*divu)[i],sdat,slo,shi);
        DEF_CLIMITS(Rho,rhodat,rholo,rhohi);
        DEF_CLIMITS(U,vel,ulo,uhi);

        DEF_CLIMITS(volume,vol,v_lo,v_hi);

        DEF_CLIMITS(area[0],areax,ax_lo,ax_hi);
        DEF_CLIMITS(area[1],areay,ay_lo,ay_hi);
#if (BL_SPACEDIM==3) 
        DEF_CLIMITS(area[2],areaz,az_lo,az_hi)
#endif
        FORT_EST_DIVU_DT(divu_ceiling,&divu_dt_factor,
                         dx,sdat,ARLIM(slo),ARLIM(shi),
                         (*dsdt)[i].dataPtr(),
                         rhodat,ARLIM(rholo),ARLIM(rhohi),
                         vel,ARLIM(ulo),ARLIM(uhi),
                         vol,ARLIM(v_lo),ARLIM(v_hi),
                         areax,ARLIM(ax_lo),ARLIM(ax_hi),
                         areay,ARLIM(ay_lo),ARLIM(ay_hi),
#if (BL_SPACEDIM==3) 
                         areaz,ARLIM(az_lo),ARLIM(az_hi),
#endif 
                         lo,hi,&dt,&min_rho_divu_ceiling);

        divu_dt = std::min(divu_dt,dt);
    }

    delete divu;
    delete dsdt;

    ParallelDescriptor::ReduceRealMin(divu_dt);

    Real rk_dt;
    if (do_rk_diffusion) rk_dt = rk_step_selection (cur_time);

    if (verbose && ParallelDescriptor::IOProcessor())
    {
	if (do_rk_diffusion)
	{
	    std::cout << "HeatTransfer::estTimeStep(): estdt, divu_dt, rk_dt = " 
		      << estdt << ", " << divu_dt << ", " << rk_dt << '\n';
	}
	else
	{
	    std::cout << "HeatTransfer::estTimeStep(): estdt, divu_dt = " 
		      << estdt << ", " << divu_dt << '\n';
	}
    }

    estdt = std::min(estdt, divu_dt);
    if (do_rk_diffusion) estdt = std::min(estdt, rk_dt);

    if (estdt < ns_estdt && ParallelDescriptor::IOProcessor())
    {
        std::cout << "HeatTransfer::estTimeStep(): timestep reduced from " 
                  << ns_estdt << " to " << estdt << '\n';
    }

    //    abort();
    return estdt;
}

void
HeatTransfer::checkTimeStep (Real dt)
{
    if (fixed_dt > 0.0 || !divu_ceiling) 
        return;

    const int   n_grow    = 1;
    const Real  cur_time  = state[State_Type].curTime();
    const Real* dx        = geom.CellSize();
    MultiFab*   dsdt      = getDsdt(0,cur_time);
    MultiFab*   divu      = getDivCond(0,cur_time);

    FArrayBox area[BL_SPACEDIM], volume;

    for (FillPatchIterator U_fpi(*this,*divu,n_grow,cur_time,State_Type,Xvel,BL_SPACEDIM);
         U_fpi.isValid();
         ++U_fpi)
    {
        const int        i   = U_fpi.index();
        FArrayBox&       U   = U_fpi();
        const FArrayBox& Rho = (*rho_ctime)[i];
        const int*       lo  = grids[i].loVect();
        const int*       hi  = grids[i].hiVect();

        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            geom.GetFaceArea(area[dir],grids,i,dir,GEOM_GROW);
        }
        geom.GetVolume(volume, grids, i, GEOM_GROW);

        DEF_LIMITS((*divu)[U_fpi],sdat,slo,shi);
        DEF_CLIMITS(Rho,rhodat,rholo,rhohi);
        DEF_CLIMITS(U,vel,ulo,uhi);

        DEF_CLIMITS(volume,vol,v_lo,v_hi);
        DEF_CLIMITS(area[0],areax,ax_lo,ax_hi);
        DEF_CLIMITS(area[1],areay,ay_lo,ay_hi);

#if (BL_SPACEDIM==3) 
        DEF_CLIMITS(area[2],areaz,az_lo,az_hi);
#endif
        FORT_CHECK_DIVU_DT(divu_ceiling,&divu_dt_factor,
                           dx,sdat,ARLIM(slo),ARLIM(shi),
			   (*dsdt)[U_fpi].dataPtr(),
                           rhodat,ARLIM(rholo),ARLIM(rhohi),
                           vel,ARLIM(ulo),ARLIM(uhi),
                           vol,ARLIM(v_lo),ARLIM(v_hi),
                           areax,ARLIM(ax_lo),ARLIM(ax_hi),
                           areay,ARLIM(ay_lo),ARLIM(ay_hi),
#if (BL_SPACEDIM==3) 
                           areaz,ARLIM(az_lo),ARLIM(az_hi),
#endif 
                           lo,hi,&dt,&min_rho_divu_ceiling);
    }

    delete dsdt;
    delete divu;
}

void
HeatTransfer::setTimeLevel (Real time,
                            Real dt_old,
                            Real dt_new)
{
    NavierStokes::setTimeLevel(time, dt_old, dt_new);    

    state[Ydot_Type].setTimeLevel(time,dt_old,dt_new);

    state[FuncCount_Type].setTimeLevel(time,dt_old,dt_new);
}

//
// This (minus the NEWMECH stuff) is copied from NavierStokes.cpp
//

void
HeatTransfer::initData ()
{
    //
    // Initialize the state and the pressure.
    //
    int         ns       = NUM_STATE - BL_SPACEDIM;
    const Real* dx       = geom.CellSize();
    MultiFab&   S_new    = get_new_data(State_Type);
    MultiFab&   P_new    = get_new_data(Press_Type);
    const Real  cur_time = state[State_Type].curTime();

// Can define in GNUmakefile
// CEG:: this allows restarting form a plotfile.  Not using this 
#ifdef BL_USE_NEWMECH
    //
    // This code has a few drawbacks.  It assumes that the physical
    // domain size of the current problem is the same as that of the
    // one that generated the pltfile.  It also assumes that the pltfile
    // has at least as many levels as does the current problem.  If
    // either of these are false this code is likely to core dump.
    //
    ParmParse pp("ht");

    std::string pltfile;
    pp.query("pltfile", pltfile);
    if (pltfile.empty())
        BoxLib::Abort("You must specify `pltfile'");
    if (ParallelDescriptor::IOProcessor())
        std::cout << "initData: reading data from: " << pltfile << std::endl;

    DataServices::SetBatchMode();
    Amrvis::FileType fileType(Amrvis::NEWPLT);
    DataServices dataServices(pltfile, fileType);

    if (!dataServices.AmrDataOk())
        //
        // This calls ParallelDescriptor::EndParallel() and exit()
        //
        DataServices::Dispatch(DataServices::ExitRequest, NULL);
    
    AmrData&                  amrData     = dataServices.AmrDataRef();
    const int                 nspecies    = getChemSolve().numSpecies();
    const Array<std::string>& names       = getChemSolve().speciesNames();   
    Array<std::string>        plotnames   = amrData.PlotVarNames();

    int idT = -1, idX = -1;
    for (int i = 0; i < plotnames.size(); ++i)
    {
        if (plotnames[i] == "temp")       idT = i;
        if (plotnames[i] == "x_velocity") idX = i;
    }
    //
    // In the plotfile the mass fractions directly follow the velocities.
    //
    int idSpec = idX + BL_SPACEDIM;

    for (int i = 0; i < BL_SPACEDIM; i++)
    {
        amrData.FillVar(S_new, level, plotnames[idX+i], Xvel+i);
        amrData.FlushGrids(idX+i);
    }
    amrData.FillVar(S_new, level, plotnames[idT], Temp);
    amrData.FlushGrids(idT);

    for (int i = 0; i < nspecies; i++)
    {
        amrData.FillVar(S_new, level, plotnames[idSpec+i], first_spec+i);
        amrData.FlushGrids(idSpec+i);
    }

    if (ParallelDescriptor::IOProcessor())
        std::cout << "initData: finished init from pltfile" << std::endl;
#endif

    for (MFIter snewmfi(S_new); snewmfi.isValid(); ++snewmfi)
    {
        BL_ASSERT(grids[snewmfi.index()] == snewmfi.validbox());

        P_new[snewmfi].setVal(0);

        const int  i       = snewmfi.index();
        RealBox    gridloc = RealBox(grids[i],geom.CellSize(),geom.ProbLo());
        const int* lo      = snewmfi.validbox().loVect();
        const int* hi      = snewmfi.validbox().hiVect();
        const int* s_lo    = S_new[snewmfi].loVect();
        const int* s_hi    = S_new[snewmfi].hiVect();
        const int* p_lo    = P_new[snewmfi].loVect();
        const int* p_hi    = P_new[snewmfi].hiVect();

#ifdef BL_USE_NEWMECH
        FORT_INITDATANEWMECH (&level,&cur_time,lo,hi,&ns,
                              S_new[snewmfi].dataPtr(Xvel),
                              S_new[snewmfi].dataPtr(BL_SPACEDIM),
                              ARLIM(s_lo), ARLIM(s_hi),
                              P_new[snewmfi].dataPtr(),
                              ARLIM(p_lo), ARLIM(p_hi),
                              dx,gridloc.lo(),gridloc.hi() );
#else
        FORT_INITDATA (&level,&cur_time,lo,hi,&ns,
                       S_new[snewmfi].dataPtr(Xvel),
                       S_new[snewmfi].dataPtr(BL_SPACEDIM),
                       ARLIM(s_lo), ARLIM(s_hi),
                       P_new[snewmfi].dataPtr(),
                       ARLIM(p_lo), ARLIM(p_hi),
                       dx,gridloc.lo(),gridloc.hi() );
#endif
    }

#ifdef BL_USE_VELOCITY
    //
    // We want to add the velocity from the supplied plotfile
    // to what we already put into the velocity field via FORT_INITDATA.
    //
    // This code has a few drawbacks.  It assumes that the physical
    // domain size of the current problem is the same as that of the
    // one that generated the pltfile.  It also assumes that the pltfile
    // has at least as many levels (with the same refinement ratios) as does
    // the current problem.  If either of these are false this code is
    // likely to core dump.
    //
    ParmParse pp("ht");

    std::string velocity_plotfile;
    pp.query("velocity_plotfile", velocity_plotfile);

    if (!velocity_plotfile.empty())
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "initData: reading data from: " << velocity_plotfile << std::endl;

        DataServices::SetBatchMode();
        Amrvis::FileType fileType(Amrvis::NEWPLT);
        DataServices dataServices(velocity_plotfile, fileType);

        if (!dataServices.AmrDataOk())
            //
            // This calls ParallelDescriptor::EndParallel() and exit()
            //
            DataServices::Dispatch(DataServices::ExitRequest, NULL);

        AmrData&                  amrData   = dataServices.AmrDataRef();
        const int                 nspecies  = getChemSolve().numSpecies();
        const Array<std::string>& names     = getChemSolve().speciesNames();   
        Array<std::string>        plotnames = amrData.PlotVarNames();

        if (amrData.FinestLevel() < level)
            BoxLib::Abort("initData: not enough levels in plotfile");

        if (amrData.ProbDomain()[level] != Domain())
            BoxLib::Abort("initData: problem domains do not match");
    
        int idX = -1;
        for (int i = 0; i < plotnames.size(); ++i)
            if (plotnames[i] == "x_velocity") idX = i;

        if (idX == -1)
            BoxLib::Abort("Could not find velocity fields in supplied velocity_plotfile");

        MultiFab tmp(S_new.boxArray(), 1, 0);
        for (int i = 0; i < BL_SPACEDIM; i++)
        {
            amrData.FillVar(tmp, level, plotnames[idX+i], 0);
            for (MFIter mfi(tmp); mfi.isValid(); ++mfi)
                S_new[mfi].plus(tmp[mfi], tmp[mfi].box(), 0, Xvel+i, 1);
            amrData.FlushGrids(idX+i);
        }

        if (ParallelDescriptor::IOProcessor())
            std::cout << "initData: finished init from velocity_plotfile" << std::endl;
    }
#endif /*BL_USE_VELOCITY*/

    make_rho_prev_time();
    make_rho_curr_time();
    //
    // Initialize other types.
    //
    initDataOtherTypes();
    //
    // Initialize divU and dSdt.
    //
    if (have_divu)
    {
        const Real dt       = 1.0;
        const Real dtin     = -1.0; // Dummy value denotes initialization.
        const Real cur_time = state[Divu_Type].curTime();
        MultiFab&  Divu_new = get_new_data(Divu_Type);

        state[State_Type].setTimeLevel(cur_time,dt,dt);

        calc_divu(cur_time,dtin,Divu_new);

        if (have_dsdt)
            get_new_data(Dsdt_Type).setVal(0);
    }

    if (state[Press_Type].descriptor()->timeType() == StateDescriptor::Point) 
    {
        get_new_data(Dpdt_Type).setVal(0);
    }

    is_first_step_after_regrid = false;
    old_intersect_new          = grids;
}

void
HeatTransfer::initDataOtherTypes ()
{
    const Real* dx = geom.CellSize();

    int add_turb = 0, box_offset[BL_SPACEDIM];
    Real turb_size[BL_SPACEDIM], turb_scale;
    //
    // FORT_CHECK_TURB should be located in PROB_?D.F.
    // If it is not, merely comment out the call below and everything
    // should be ok.
    //
    FORT_CHECK_TURB(&add_turb,turb_size,box_offset,&turb_scale,dx);
  
    if (add_turb) 
    {
        MultiFab& S_new = get_new_data(State_Type);

        const int numVelComp = BL_SPACEDIM;
        const int numVelGrow = 0;
        //
        // Read in data about turbulent velocity field.
        //
        Real fileProbSize[BL_SPACEDIM];
        int vel_size[BL_SPACEDIM];
        {
            std::string dxFileName = turbFile + "/Size";
            std::ifstream is(dxFileName.c_str(),std::ios::in);
            if (is.fail())
            {
                std::string msg("HeatTransfer: cannot open velocity file: ");
                msg += turbFile;
                BoxLib::Error(msg.c_str());
            }
            for (int i = 0; i < BL_SPACEDIM; i++)
                is >> fileProbSize[i];
            for (int i = 0; i < BL_SPACEDIM; i++)
                is >> vel_size[i];
        }
        //
        // Make sure turbulent velocity field is correct size.
        //
        if (D_TERM(fileProbSize[0] != turb_size[0],
                   || fileProbSize[1] != turb_size[1],
                   || fileProbSize[2] != turb_size[2]))
        {
            if (ParallelDescriptor::IOProcessor())
                std::cout << "warning turb field is wrong size" << '\n';
        }
        //
        // Read in turbulent velocity field ... store in turbVelFile.
        //
        std::string MFPath = turbFile +"/MultiFab";
        MultiFab* turbVelFile = new MultiFab();
        VisMF::Read(*turbVelFile,MFPath);
        //
        // Compute refinement ratio between turbulence file data and this level.
        // 
        bool    do_avgdown;
        IntVect avgdown_ratio(IntVect::TheZeroVector());
        IntVect refine_ratio(IntVect::TheZeroVector());

        for (int i = 0;i < BL_SPACEDIM; i++)
        {
            Real dxFile = fileProbSize[i]/Real(vel_size[i]);
            if (dxFile > geom.CellSize(i))
            {
                refine_ratio[i] = int(dxFile/geom.CellSize(i));
                do_avgdown = false;
            }
            else
            {
                avgdown_ratio[i] = int(geom.CellSize(i)/dxFile);
                do_avgdown = true;
            }
        }
        //
        // Check for consistency in ratios.
        //
        if (do_avgdown) 
        {
            if (D_TERM(avgdown_ratio[0] < 1, 
                       || avgdown_ratio[1] < 1, 
                       || avgdown_ratio[2] < 1)
                || refine_ratio != IntVect::TheZeroVector())
                BoxLib::Error("bad sizing");
        }
        else
        {
            if (D_TERM(refine_ratio[0] < 1, 
                       || refine_ratio[1] < 1, 
                       || refine_ratio[2] < 1)
                || avgdown_ratio != IntVect::TheZeroVector())
                BoxLib::Error("bad sizing");
        }

        MultiFab* gridTurbVel;

        if (do_avgdown) 
        {
            //
            // Avg down data from fine level to this level.
            // The case where they are on the same level falls through here.
            //
            if (ParallelDescriptor::IOProcessor())
                std::cout << "averaging down turb data by factor of " 
                          << avgdown_ratio << std::endl;

            BoxArray fileTurbBA(turbVelFile->boxArray());
            BoxArray crseTurbBA(fileTurbBA);
            crseTurbBA.coarsen(avgdown_ratio);
            gridTurbVel = new MultiFab(crseTurbBA,numVelComp,numVelGrow);
	  
            Box fineDomain = BoxLib::refine(geom.Domain(),avgdown_ratio);
            Geometry fineGeom(fineDomain);
            MultiFab* crseVol = new MultiFab;
            MultiFab* fineVol = new MultiFab;
            geom.GetVolume(*crseVol,crseTurbBA,numVelGrow);
            fineGeom.GetVolume(*fineVol,fileTurbBA,numVelGrow);
	  
            for (MFIter finemfi(*turbVelFile); finemfi.isValid(); ++finemfi)
            {
                const int i       = finemfi.index();
                const int f_level = 0;
                const int c_level = 0;
                const Box ovlp    = (*gridTurbVel).box(i);
	      
                NavierStokes::avgDown_doit((*turbVelFile)[finemfi],
                                           (*gridTurbVel)[finemfi],
                                           (*fineVol)[finemfi],
                                           (*crseVol)[finemfi],
                                           f_level,c_level,
                                           ovlp,Xvel,numVelComp,avgdown_ratio);
            }

            delete crseVol;
            delete fineVol;
        }
        else
        {
            //
            // Interpolate data from this level to finer level.
            //
            if (ParallelDescriptor::IOProcessor())
                std::cout << "interpolating turb data by factor of " 
                          << refine_ratio << std::endl;

            if (turbVelFile->nGrow() == 0)
                BoxLib::Error("need ghost cells to interp -- use new turb fcn");
            BoxArray fileTurbBA(turbVelFile->boxArray());
            BoxArray gridTurbBA(fileTurbBA);
            gridTurbBA.refine(refine_ratio);
            gridTurbVel = new MultiFab(gridTurbBA,numVelComp,numVelGrow);

            Box fineDomain = BoxLib::refine(geom.Domain(),refine_ratio);
            Geometry fineGeom(fineDomain);
            const Geometry& crseGeom = geom;
            Array<BCRec> bcr(numVelComp);
            Interpolater* mapper = &pc_interp;
            const Box& pdomain = state[State_Type].getDomain();
            const StateDescriptor&  desc    = desc_lst[State_Type];

            for (MFIter crsemfi(*turbVelFile); crsemfi.isValid(); ++crsemfi)
            {
                const int  i    = crsemfi.index();
                const Box& dbox = gridTurbBA[i];
                BoxLib::setBC(dbox,pdomain,Xvel,Xvel,numVelComp,desc.getBCs(),bcr);
                mapper->interp((*turbVelFile)[crsemfi],Xvel,
                               (*gridTurbVel)[crsemfi],Xvel,
                               numVelComp, (*gridTurbVel).box(i),
                               refine_ratio,
                               crseGeom,fineGeom,
                               bcr);
            }
        }

        delete turbVelFile;
        //
        // Shift grid velocity by offset.
        //
        BoxArray gridTurbBA = gridTurbVel->boxArray();
        BoxArray shiftedGridTurbBA(gridTurbBA);
        for (int dir=0; dir < BL_SPACEDIM; dir++)
            shiftedGridTurbBA.shift(dir,box_offset[dir]);
        MultiFab* shiftedGridTurbVel
            = new MultiFab(shiftedGridTurbBA,numVelComp,numVelGrow);

        for (MFIter gridmfi(*gridTurbVel); gridmfi.isValid(); ++gridmfi)
        {

            (*shiftedGridTurbVel)[gridmfi].copy((*gridTurbVel)[gridmfi],
                                                gridmfi.validbox(),Xvel,
                                                (*shiftedGridTurbVel).box(gridmfi.index()),
                                                Xvel,numVelComp);
        }

        delete gridTurbVel;
        //
        // Copy vel into boxarray used by S_new,
        // multiply by turbulence scale and then add to S_new.
        //
        MultiFab* Vel = new MultiFab(S_new.boxArray(),numVelComp,numVelGrow);
        Vel->setVal(0.0);
        Vel->copy(*shiftedGridTurbVel,Xvel,Xvel,numVelComp);

        Vel->mult(turb_scale,Xvel,numVelComp,numVelGrow);

        S_new.plus(*Vel,Xvel,numVelComp,numVelGrow);

        delete shiftedGridTurbVel;
        delete Vel;
    }

    const Real cur_time  = state[State_Type].curTime();
    {
        MultiFab rhoh(grids,1,0);
        compute_rhohmix(cur_time,rhoh);
        get_new_data(State_Type).copy(rhoh,0,RhoH,1);
    }
    //
    // Set up diffusivities, viscosities (need for initial divu compute)
    //
    const Real dt        = -1.0;
    const int  iteration = 0;
    const int  ncycle    = 0;
    //
    // Assume always variable diffusivity.
    // Assume always variable viscosity.
    //
    const int offset   = BL_SPACEDIM + 1;
    const int num_diff = NUM_STATE-offset;
    calcDiffusivity(cur_time, dt, iteration, ncycle, offset, num_diff, true);
    //
    // Assume that by now, S_new has "good" data
    //
    get_new_data(Ydot_Type).setVal(0);

    get_new_data(FuncCount_Type).setVal(1);

    setThermoPress(cur_time);
}

void
HeatTransfer::init (AmrLevel& old)
{
    NavierStokes::init(old);

    HeatTransfer* oldht    = (HeatTransfer*) &old;
    const Real    cur_time = oldht->state[State_Type].curTime();
    //
    // Get best ydot data.
    //
    MultiFab& Ydot = get_new_data(Ydot_Type);

    for (FillPatchIterator fpi(*oldht,Ydot,Ydot.nGrow(),cur_time,Ydot_Type,0,nspecies);
         fpi.isValid();
         ++fpi)
    {
        Ydot[fpi.index()].copy(fpi());
    }

    RhoH_to_Temp(get_new_data(State_Type));

    MultiFab& FuncCount = get_new_data(FuncCount_Type);

    for (FillPatchIterator fpi(*oldht,FuncCount,FuncCount.nGrow(),cur_time,FuncCount_Type,0,1);
         fpi.isValid();
         ++fpi)
    {
        FuncCount[fpi.index()].copy(fpi());
    }
}

//
// Inits the data on a new level that did not exist before regridding.
//

void
HeatTransfer::init ()
{
    NavierStokes::init();
 
    HeatTransfer& old      = getLevel(level-1);
    const Real    cur_time = old.state[State_Type].curTime();
    //
    // Get best ydot data.
    //
    FillCoarsePatch(get_new_data(Ydot_Type),0,cur_time,Ydot_Type,0,nspecies);

    RhoH_to_Temp(get_new_data(State_Type));

    if (new_T_threshold>0)
    {
        MultiFab& crse = old.get_new_data(State_Type);
        MultiFab& fine = get_new_data(State_Type);

        RhoH_to_Temp(crse,0); // Make sure T is current
        Real min_T_crse = crse.min(Temp);
        Real min_T_fine = min_T_crse * std::min(1.0, new_T_threshold);

        const int* ratio = crse_ratio.getVect();
        int Tcomp = (int)Temp;
        int Rcomp = (int)Density;
        int n_tmp = std::max( (int)Density, std::max( (int)Temp, std::max( last_spec, RhoH) ) );
        Array<Real> tmp(n_tmp);
        int num_cells_hacked = 0;
        for (MFIter mfi(fine); mfi.isValid(); ++mfi)
        {
            FArrayBox& fab = fine[mfi];
            const Box& box = mfi.validbox();
            num_cells_hacked += 
                FORT_CONSERVATIVE_T_FLOOR(box.loVect(), box.hiVect(),
                                          fab.dataPtr(), ARLIM(fab.loVect()), ARLIM(fab.hiVect()),
                                          &min_T_fine, &Tcomp, &Rcomp, &first_spec, &last_spec, &RhoH,
                                          ratio, tmp.dataPtr(), &n_tmp);
        }

        ParallelDescriptor::ReduceIntSum(num_cells_hacked);

        if (num_cells_hacked > 0) {

            Real old_min = fine.min(Temp);
            RhoH_to_Temp(get_new_data(State_Type));
            Real new_min = fine.min(Temp);

            if (verbose && ParallelDescriptor::IOProcessor()) {
                std::cout << "...level data adjusted to reduce new extrema (" << num_cells_hacked
                          << " cells affected), new min = " << new_min << " (old min = " << old_min << ")" << std::endl;
            }
        }
    }

    FillCoarsePatch(get_new_data(FuncCount_Type),0,cur_time,FuncCount_Type,0,1);
}

void
HeatTransfer::post_timestep (int crse_iteration)
{
    NavierStokes::post_timestep(crse_iteration);

    if (plot_reactions && level == 0)
    {
        const int Ndiag = auxDiag["REACTIONS"]->nComp();

        //
        // Multiply by the inverse of the coarse timestep.
        //
        const Real factor = 1.0 / crse_dt;

        for (int i = parent->finestLevel(); i >= 0; --i)
            getLevel(i).auxDiag["REACTIONS"]->mult(factor);

        for (int i = parent->finestLevel(); i > 0; --i)
        {
            MultiFab fvolume;
            MultiFab cvolume;

            HeatTransfer& clev = getLevel(i-1);
            HeatTransfer& flev = getLevel(i);

            MultiFab& Ydot_crse = *(clev.auxDiag["REACTIONS"]);
            MultiFab& Ydot_fine = *(flev.auxDiag["REACTIONS"]);

            flev.geom.GetVolume(fvolume,flev.grids,GEOM_GROW);
            clev.geom.GetVolume(cvolume,clev.grids,GEOM_GROW);
            
            NavierStokes::avgDown(clev.boxArray(),flev.boxArray(),
                                  Ydot_crse,Ydot_fine,
                                  cvolume,fvolume,
                                  i-1,i,0,Ndiag,parent->refRatio(i-1));
        }
    }
}

void
HeatTransfer::post_restart ()
{
    NavierStokes::post_restart();

    Real dummy  = 0;
    int MyProc  = ParallelDescriptor::MyProc();
    int step    = parent->levelSteps(0);
    int restart = 1;

    if (do_active_control)
        FORT_ACTIVECONTROL(&dummy,&dummy,&crse_dt,&MyProc,&step,&restart);
}

void
HeatTransfer::post_regrid (int lbase,
                           int new_finest)
{
    NavierStokes::post_regrid(lbase, new_finest);
    //
    // FIXME: This may be necessary regardless, unless the interpolation
    //        to fine from coarse data preserves rho=sum(rho.Y)
    //
    if (do_set_rho_to_species_sum)
    {
        const int nGrow = 0;
        if (parent->levelSteps(0)>0 && level>lbase)
            set_rho_to_species_sum(get_new_data(State_Type),0,nGrow,0);
    }
}

void
HeatTransfer::post_init (Real stop_time)
{
    if (level > 0)
        //
        // Nothing to sync up at level > 0.
        //
        return;

    const Real cur_time     = state[State_Type].curTime();
    const int  finest_level = parent->finestLevel();
    Real        dt_init     = 0.0;

    Array<Real> dt_save(finest_level+1);
    Array<int>  nc_save(finest_level+1);
    Real        dt_init2 = 0.0;
    Array<Real> dt_save2(finest_level+1);
    Array<int>  nc_save2(finest_level+1);
    //
    // Ensure state is consistent, i.e. velocity field satisfies initial
    // estimate of constraint, coarse levels are fine level averages, pressure
    // is zero.
    //
    post_init_state();
    //
    // Estimate the initial timestepping.
    //
    post_init_estDT(dt_init,nc_save,dt_save,stop_time);
    //
    // Better estimate needs dt to estimate divu
    //
    const bool do_iter        = do_init_proj && projector;
    const int  init_divu_iter = do_iter ? num_divu_iters : 0;

    if (verbose && ParallelDescriptor::IOProcessor())
      std::cout << "doing num_divu_iters = " << num_divu_iters << std::endl;

    for (int iter = 0; iter < init_divu_iter; ++iter)
    {
        //
        // Update species destruction rates in each level but not state.
        //
        if (nspecies > 0)
        {
            for (int k = 0; k <= finest_level; k++)
            {
                MultiFab& S_new = getLevel(k).get_new_data(State_Type);
                //
                // Don't update S_new in this strang_chem() call ...
                //
                MultiFab S_tmp(S_new.boxArray(),S_new.nComp(),0);

                S_tmp.copy(S_new);  // Parallel copy
		if (ParallelDescriptor::IOProcessor())
		  std::cout<<"post_init::calling strang_chem() \n";
                getLevel(k).strang_chem(S_tmp,dt_save[k],HT_EstimateYdotNew);
            }
        }
        //
        // Recompute the velocity to obey constraint with chemistry and
        // divqrad and then average that down.
        //
        if (nspecies > 0)
        {
            for (int k = 0; k <= finest_level; k++)
            {
                MultiFab&  Divu_new = getLevel(k).get_new_data(Divu_Type);
                getLevel(k).calc_divu(cur_time,dt_save[k],Divu_new);
            }
            if (!hack_noavgdivu)
            {
                for (int k = finest_level-1; k >= 0; k--)
                {
                    MultiFab fvolume;
                    MultiFab cvolume;

                    HeatTransfer&   fine_lev = getLevel(k+1);
                    const BoxArray& fgrids   = fine_lev.grids;
                    
                    HeatTransfer&   crse_lev = getLevel(k);
                    const BoxArray& cgrids   = crse_lev.grids;
                    const IntVect&  fratio   = crse_lev.fine_ratio;
                    
                    MultiFab& Divu_crse = crse_lev.get_new_data(Divu_Type);
                    MultiFab& Divu_fine = fine_lev.get_new_data(Divu_Type);

                    fine_lev.geom.GetVolume(fvolume,fine_lev.grids,GEOM_GROW);
                    crse_lev.geom.GetVolume(cvolume,crse_lev.grids,GEOM_GROW);
                    
                    crse_lev.NavierStokes::avgDown(cgrids,fgrids,
                                                   Divu_crse,Divu_fine,
                                                   cvolume,fvolume,
                                                   k,k+1,0,1,fratio);
                }
            }
            //
            // Recompute the initial velocity field based on this new constraint
            //
            const Real divu_time = state[Divu_Type].curTime();

            int havedivu = 1;

            projector->initialVelocityProject(0,divu_time,havedivu);
            //
            // Average down the new velocity
            //
            for (int k = finest_level-1; k >= 0; k--)
            {
                MultiFab fvolume;
                MultiFab cvolume;

                getLevel(k+1).geom.GetVolume(fvolume,getLevel(k+1).grids,GEOM_GROW);
                getLevel(k).geom.GetVolume(cvolume,getLevel(k).grids,GEOM_GROW);

                const BoxArray& fgrids  = getLevel(k+1).grids;
                const BoxArray& cgrids  = getLevel(k).grids;
                MultiFab&       S_fine  = getLevel(k+1).get_new_data(State_Type);
                MultiFab&       S_crse  = getLevel(k).get_new_data(State_Type);
                IntVect&        fratio  = getLevel(k).fine_ratio;
                
                getLevel(k).NavierStokes::avgDown(cgrids,fgrids,
                                                  S_crse,S_fine,
                                                  cvolume,fvolume,
                                                  k,k+1,Xvel,BL_SPACEDIM,
                                                  fratio);
            }
        }
        //
        // Estimate the initial timestepping again, using new velocity
        // (necessary?) Need to pass space to save dt, nc, but these are
        // hacked, just pass something.
        //
	// reset Ncycle to nref...
        //
	parent->setNCycle(nc_save);
        post_init_estDT(dt_init2, nc_save2, dt_save2, stop_time);
	//
	// Compute dt_init,dt_save as the minimum of the values computed
	// in the calls to post_init_estDT
	// Then setTimeLevel and dt_level to these values.
	//
	dt_init = std::min(dt_init,dt_init2);
	Array<Real> dt_level(finest_level+1,dt_init);

	parent->setDtLevel(dt_level);
	for (int k = 0; k <= finest_level; k++)
        {
	    dt_save[k] = std::min(dt_save[k],dt_save2[k]);
	    getLevel(k).setTimeLevel(cur_time,dt_init,dt_init);
        }
    }
    //
    // Initialize the pressure by iterating the initial timestep.
    //
    post_init_press(dt_init, nc_save, dt_save);
    //
    // Compute the initial estimate of conservation.
    //
    if (sum_interval > 0)
        sum_integrated_quantities();
}

void
HeatTransfer::sum_integrated_quantities ()
{
    const int finest_level = parent->finestLevel();
    const Real time        = state[State_Type].curTime();

    Real mass = 0.0;
    for (int lev = 0; lev <= finest_level; lev++)
	mass += getLevel(lev).volWgtSum("density",time);

    if (ParallelDescriptor::IOProcessor())
        std::cout << "TIME= " << time << " MASS= " << mass;

    if (getChemSolve().index(fuelName) >= 0)
    {
        Real fuelmass = 0.0;
        std::string fuel = "rho.Y(" + fuelName + ")";
        for (int lev = 0; lev <= finest_level; lev++)
            fuelmass += getLevel(lev).volWgtSum(fuel,time);

        if (ParallelDescriptor::IOProcessor())
            std::cout << " FUELMASS= " << fuelmass;

        int MyProc  = ParallelDescriptor::MyProc();
        int step    = parent->levelSteps(0);
        int restart = 0;

        if (do_active_control)
            FORT_ACTIVECONTROL(&fuelmass,&time,&crse_dt,&MyProc,&step,&restart);
    }

    if (ParallelDescriptor::IOProcessor())
        std::cout << '\n';

    Real rho_h    = 0.0;
    Real rho_temp = 0.0;

    for (int lev = 0; lev <= finest_level; lev++)
    {
        rho_temp += getLevel(lev).volWgtSum("rho_temp",time);
        rho_h     = getLevel(lev).volWgtSum("rhoh",time);
        if (ParallelDescriptor::IOProcessor())
            std::cout << "TIME= " << time << " LEV= " << lev << " RHOH= " << rho_h << '\n';
    }
    if (ParallelDescriptor::IOProcessor())
        std::cout << "TIME= " << time << " RHO*TEMP= " << rho_temp << '\n';

    int old_prec = std::cout.precision(15);

    Real min_temp = 1.0e20;
    Real max_temp = -1.0e20;
    Real min_xvel = 1.0e20;
    Real max_xvel = -1.0e20;
    Real min_yvel = 1.0e20;
    Real max_yvel = -1.0e20;
#if (BL_SPACEDIM == 3)
    Real min_zvel = 1.0e20;
    Real max_zvel = -1.0e20;
#endif

    for (int lev = 0; lev <= finest_level; lev++)
    {
	MultiFab& newstate = getLevel(lev).get_data(State_Type,time);

        min_temp = std::min(min_temp,newstate.min(Temp,0));
        max_temp = std::max(max_temp,newstate.max(Temp,0));
	min_xvel = std::min(min_xvel,newstate.min(Xvel,0));
	max_xvel = std::max(max_xvel,newstate.max(Xvel,0));
	min_yvel = std::min(min_yvel,newstate.min(Yvel,0));
	max_yvel = std::max(max_yvel,newstate.max(Yvel,0));
#if (BL_SPACEDIM == 3)
	min_zvel = std::min(min_zvel,newstate.min(Zvel,0));
	max_zvel = std::max(max_zvel,newstate.max(Zvel,0));
#endif
    }

    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "min,max temp = " << min_temp << ", " << max_temp << '\n';

        std::cout << "min,max xvel = " << min_xvel << ", " << max_xvel << '\n';
        std::cout << "min,max yvel = " << min_yvel << ", " << max_yvel << '\n';
#if (BL_SPACEDIM == 3)
        std::cout << "min,max zvel = " << min_zvel << ", " << max_zvel << '\n';
#endif
    }

    const int IOProc = ParallelDescriptor::IOProcessorNumber();

    if (nspecies > 0)
    {
        Real min_max_sum[2] = { 1.0e20, -1.0e20 };

	for (int lev = 0; lev <= finest_level; lev++)
	{
            MultiFab* mf = getLevel(lev).derive("rhominsumrhoY",time,0);

            for (MFIter mfi(*mf); mfi.isValid(); ++mfi)
	    {
		min_max_sum[0] = std::min(min_max_sum[0],(*mf)[mfi].min());
		min_max_sum[1] = std::max(min_max_sum[1],(*mf)[mfi].max());
	    }

            delete mf;
	}

        ParallelDescriptor::ReduceRealMin(min_max_sum,2,IOProc);

        if (ParallelDescriptor::IOProcessor())
        {
            std::cout << "min,max rho-sum rho Y_l = "
                      << min_max_sum[0] << ", "
                      << min_max_sum[1] << '\n';
        }
    }

    Real min_max_sum[2] = { 1.0e20, -1.0e20 };
    
    for (int lev = 0; lev <= finest_level; lev++)
    {
        MultiFab* mf = getLevel(lev).derive("sumYdot",time,0);

        for (MFIter mfi(*mf); mfi.isValid(); ++mfi)
        {
            min_max_sum[0] = std::min(min_max_sum[0],(*mf)[mfi].min());
            min_max_sum[1] = std::max(min_max_sum[1],(*mf)[mfi].max());
        }

        delete mf;
    }

    ParallelDescriptor::ReduceRealMin(min_max_sum,2,IOProc);

    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "min,max sum Ydot = "
                  << min_max_sum[0] << ", "
                  << min_max_sum[1] << '\n';
    }

    std::cout << std::setprecision(old_prec);
}

void
HeatTransfer::post_init_press (Real&        dt_init,
			       Array<int>&  nc_save,
			       Array<Real>& dt_save)
{
    const int  nState          = desc_lst[State_Type].nComp();
    const int  nGrow           = 0;
    const Real cur_time        = state[State_Type].curTime();
    const int  finest_level    = parent->finestLevel();
    MultiFab&  P_new           = get_new_data(Press_Type);
    MultiFab&  P_old           = get_old_data(Press_Type);
    NavierStokes::initial_iter = true;
    //
    // Make space to save a copy of the initial State_Type state data
    //
    PArray<MultiFab> saved_state(finest_level+1,PArrayManage);

    if (init_iter > 0)
        for (int k = 0; k <= finest_level; k++)
            saved_state.set(k,new MultiFab(getLevel(k).grids,nState,nGrow));
    //
    // Iterate over the advance function.
    //
    for (int iter = 0; iter < init_iter; iter++)
    {
	//
	// Squirrel away copy of pre-advance State_Type state
	//
        for (int k = 0; k <= finest_level; k++)
	    MultiFab::Copy(saved_state[k],
                           getLevel(k).get_new_data(State_Type),
			   0,
                           0,
                           nState,
                           nGrow);

        for (int k = 0; k <= finest_level; k++ )
        {
	  if (verbose && ParallelDescriptor::IOProcessor())
	    std::cout<<"post_init_press..\n";

            getLevel(k).advance(cur_time,dt_init,1,1);
        }
        //
        // This constructs a guess at P, also sets p_old == p_new.
        //
        MultiFab** sig = new MultiFab*[finest_level+1];

        for (int k = 0; k <= finest_level; k++)
        {
            sig[k] = getLevel(k).get_rho_half_time();
        }

        if (projector)
        {
            int havedivu = 1;
            projector->initialSyncProject(0,sig,parent->dtLevel(0),cur_time,
                                          dt_init,havedivu);
        }
        delete [] sig;

        for (int k = finest_level-1; k>= 0; k--)
        {
            getLevel(k).avgDown();
        }
        for (int k = 0; k <= finest_level; k++)
        {
            //
            // Reset state variables to initial time, but
            // do not pressure variable, only pressure time.
            //
            getLevel(k).resetState(cur_time, dt_init, dt_init);
        }
	//
	// For State_Type state, restore state we saved above
	//
        for (int k = 0; k <= finest_level; k++)
	    MultiFab::Copy(getLevel(k).get_new_data(State_Type),
                           saved_state[k],
			   0,
                           0,
                           nState,
                           nGrow);

        NavierStokes::initial_iter = false;
    }

    if (init_iter <= 0)
        NavierStokes::initial_iter = false; // Just being compulsive -- rbp.

    NavierStokes::initial_step = false;
    //
    // Re-instate timestep.
    //
    for (int k = 0; k <= finest_level; k++)
    {
        getLevel(k).setTimeLevel(cur_time,dt_save[k],dt_save[k]);
        if (getLevel(k).state[Press_Type].descriptor()->timeType() ==
            StateDescriptor::Point)
          getLevel(k).state[Press_Type].setNewTimeLevel(cur_time+.5*dt_init);
    }
    parent->setDtLevel(dt_save);
    parent->setNCycle(nc_save);
}

//
// Reset the time levels to time (time) and timestep dt.
// This is done at the start of the timestep in the pressure
// iteration section.
//

void
HeatTransfer::resetState (Real time,
                          Real dt_old,
                          Real dt_new)
{
    NavierStokes::resetState(time,dt_old,dt_new);

    state[Ydot_Type].reset();
    state[Ydot_Type].setTimeLevel(time,dt_old,dt_new);

    state[FuncCount_Type].reset();
    state[FuncCount_Type].setTimeLevel(time,dt_old,dt_new);
}

void
HeatTransfer::avgDown ()
{
    if (level == parent->finestLevel()) return;

    HeatTransfer&   fine_lev = getLevel(level+1);
    const BoxArray& fgrids   = fine_lev.grids;
    MultiFab&       S_crse   = get_new_data(State_Type);
    MultiFab&       S_fine   = fine_lev.get_new_data(State_Type);

    MultiFab fvolume, volume;
    geom.GetVolume(volume,grids,GEOM_GROW);
    fine_lev.geom.GetVolume(fvolume,fine_lev.grids,GEOM_GROW);

    NavierStokes::avgDown(grids,fgrids,S_crse,S_fine,volume,fvolume,
                          level,level+1,0,S_crse.nComp(),fine_ratio);
    //
    // Fill rho_ctime at the current and finer levels with the correct data.
    //
    for (int lev = level; lev <= parent->finestLevel(); lev++)
    {
        getLevel(lev).make_rho_curr_time();
    }
    //
    // Reset the temperature
    //
    RhoH_to_Temp(S_crse);
    //
    // Now average down pressure over time n-(n+1) interval.
    //
    MultiFab&       P_crse      = get_new_data(Press_Type);
    MultiFab&       P_fine_init = fine_lev.get_new_data(Press_Type);
    MultiFab&       P_fine_avg  = *fine_lev.p_avg;
    MultiFab&       P_fine      = initial_step ? P_fine_init : P_fine_avg;
    const BoxArray& P_cgrids    = state[Press_Type].boxArray();
    const BoxArray& P_fgrids    = fine_lev.state[Press_Type].boxArray();

    BoxArray crse_P_fine_BA(P_fgrids.size());

    for (int i = 0; i < P_fgrids.size(); ++i)
    {
        crse_P_fine_BA.set(i,BoxLib::coarsen(P_fgrids[i],fine_ratio));
    }

    {
        MultiFab crse_P_fine(crse_P_fine_BA,1,0);

        for (MFIter mfi(P_fine); mfi.isValid(); ++mfi)
        {
            const int i = mfi.index();

            injectDown(crse_P_fine_BA[i],crse_P_fine[i],P_fine[i],fine_ratio);
        }

        P_crse.copy(crse_P_fine);  // Parallel copy
    }
    //
    // Next average down divu and dSdT at new time.
    //
    if (hack_noavgdivu) 
    {
        //
        // Now that state averaged down, recompute divu (don't avgDown,
        // since that will give a very different value, and screw up mac)
        //
        StateData& divuSD = get_state_data(Divu_Type);// should be const...
        const Real time   = divuSD.curTime();
        const Real dt     = time - divuSD.prevTime();
        calc_divu(time,dt,divuSD.newData());
    }
    else
    {
        MultiFab& Divu_crse = get_new_data(Divu_Type);
        MultiFab& Divu_fine = fine_lev.get_new_data(Divu_Type);
            
        NavierStokes::avgDown(grids,fgrids,
                              Divu_crse,Divu_fine,
                              volume,fvolume,
                              level,level+1,0,1,fine_ratio);
    }

    if (hack_noavgdivu)
    {
        //
        //  Now that have good divu, recompute dsdt (don't avgDown,
        //   since that will give a very different value, and screw up mac)
        //
        StateData& dsdtSD = get_state_data(Dsdt_Type);// should be const...
        MultiFab&  dsdt   = dsdtSD.newData();

        if (get_state_data(Divu_Type).hasOldData())
        {
            const Real time = dsdtSD.curTime();
            const Real dt   = time - dsdtSD.prevTime();
            calc_dsdt(time,dt,dsdt);
        }
        else
        {
            dsdt.setVal(0.0);
        }
    }
    else
    {
        MultiFab& Dsdt_crse = get_new_data(Dsdt_Type);
        MultiFab& Dsdt_fine = fine_lev.get_new_data(Dsdt_Type);
            
        NavierStokes::avgDown(grids,fgrids,
                              Dsdt_crse,Dsdt_fine,
                              volume,fvolume,
                              level,level+1,0,1,fine_ratio);
    }
}

void
HeatTransfer::scalar_diffusion_update (Real dt,
                                       int  first_scalar, 
                                       int  last_scalar,
                                       int  corrector)
{
    const Real strt_time = ParallelDescriptor::second();
    //
    // Build single component edge-centered array of MultiFabs for fluxes
    //
    MultiFab** fluxSCn;
    MultiFab** fluxSCnp1;
    const int nGrow = 0;
    const int nComp = 1;
    diffusion->allocFluxBoxesLevel(fluxSCn  ,nGrow,nComp);
    diffusion->allocFluxBoxesLevel(fluxSCnp1,nGrow,nComp);
    //
    // Set diffusion solve mode.
    //
    Diffusion::SolveMode solve_mode = Diffusion::ONEPASS;
    //
    // Do implicit c-n solve for each scalar.
    //
    const MultiFab* Rh = get_rho_half_time();

    for (int sigma = first_scalar; sigma <= last_scalar; sigma++)
    {
        if (is_diffusive[sigma])
        {
            int rho_flag = 0;
	    
            MultiFab *delta_rhs, *alpha, **betan, **betanp1;

            diffuse_scalar_setup(dt,sigma,&rho_flag,delta_rhs,alpha,betan,betanp1);
            //
            // Note: dt taken care of in diffuse_scalar.
            //
	    const int dataComp = 0; // Component of dR, alpha, betas to use.

	    //FIXME
// 	    if(sigma == RhoH)
// 	    {
// 	      VisMF::Write(*betan[0],"bxn_ht");    
// 	      VisMF::Write(*betan[1],"byn_ht");    
// 	      VisMF::Write(*betanp1[0],"bxn1_ht");    
// 	      VisMF::Write(*betanp1[1],"byn1_ht"); 
//    	      VisMF::Write(*fluxSCn[0],"fscxn_ht");    
// 	      VisMF::Write(*fluxSCn[1],"fscyn_ht");    
// 	      VisMF::Write(*fluxSCnp1[0],"fscxn1_ht");    
// 	      VisMF::Write(*fluxSCnp1[1],"fscyn1_ht");
//  	      VisMF::Write(*Rh,"rh_ht");       
// 	      VisMF::Write(*delta_rhs,"delta_ht");       
// 	      std::cout<<" dt = "<<dt<<std::endl;
// 	      std::cout<<" sigma = "<<sigma<<std::endl;
// 	      std::cout<<" theta = "<<be_cn_theta<<std::endl;
// 	      std::cout<<" rho_flag = "<<rho_flag<<std::endl;
// 	      std::cout<<" dataComp = "<<dataComp<<std::endl;
// 	      std::cout<<" solve mode = "<<solve_mode<<std::endl;
// 	    }

            diffusion->diffuse_scalar(dt,sigma,be_cn_theta,Rh,rho_flag,fluxSCn,
                                      fluxSCnp1,dataComp,delta_rhs,alpha,betan,
                                      betanp1,solve_mode);
	    //
	    // If corrector, increment the viscous flux registers (assume
	    //  corrector called only ONCE!).
	    //
	    if (do_reflux && corrector)
	    {
                {
                    FArrayBox fluxtot;
                    for (int d = 0; d < BL_SPACEDIM; d++)
                    {
                        for (MFIter fmfi(*fluxSCn[d]); fmfi.isValid(); ++fmfi)
                        {
                            const Box& ebox = (*fluxSCn[d])[fmfi].box();
                            fluxtot.resize(ebox,nComp);
                            fluxtot.copy((*fluxSCn[d])[fmfi],ebox,
					 0,ebox,0,nComp);
                            fluxtot.plus((*fluxSCnp1[d])[fmfi],ebox,
					 0,0,nComp);
                            if (level < parent->finestLevel())
			    {
			      getLevel(level+1).getViscFluxReg().
				CrseInit(fluxtot,ebox,d,0,sigma,nComp,-dt);
			    }
                            if (level > 0)
                                getViscFluxReg().FineAdd(fluxtot,d,
							 fmfi.index(),
                                                         0,sigma,nComp,dt);
                        }
                    }
                }
		if (level < parent->finestLevel())
		    getLevel(level+1).getViscFluxReg().CrseInitFinish();
	    }
	    //
	    // Clean up memory, etc
	    //
            diffuse_cleanup(delta_rhs, betan, betanp1, alpha);
        }  
    }    
    diffusion->removeFluxBoxesLevel(fluxSCn);
    diffusion->removeFluxBoxesLevel(fluxSCnp1);

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;

    ParallelDescriptor::ReduceRealMax(run_time,IOProc);

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::scalar_diffusion_update(): time: " << run_time << std::endl;
}

void
HeatTransfer::differential_spec_diffusion_update (Real dt,
						  int  corrector)
{
    const Real strt_time = ParallelDescriptor::second();

    if (hack_nospecdiff)
    {
        if (verbose && ParallelDescriptor::IOProcessor())
            std::cout << "... HACK!!! skipping spec diffusion " << std::endl;

        if (!corrector)
            MultiFab::Copy(get_new_data(State_Type),get_old_data(State_Type),first_spec,first_spec,nspecies,0);

        for (int d = 0; d < BL_SPACEDIM; ++d)
        {
            SpecDiffusionFluxn[d]->setVal(0,0,nspecies);
            SpecDiffusionFluxnp1[d]->setVal(0,0,nspecies);
        }
        for (int comp = 0; comp < nspecies; ++comp)
            spec_diffusion_flux_computed[comp] = HT_Diffusion;

        return;
    }
    //
    // Build single component edge-centered array of MultiFabs for fluxes
    //
    MultiFab** fluxSCn;
    MultiFab** fluxSCnp1;
    const int nGrow   = 0;
    const int nCompSC = 1;
    const int sCompSC = 0;
    diffusion->allocFluxBoxesLevel(fluxSCn,  nGrow,nCompSC);
    diffusion->allocFluxBoxesLevel(fluxSCnp1,nGrow,nCompSC);
    //
    // Set diffusion solve mode
    //
    Diffusion::SolveMode solve_mode = Diffusion::PREDICTOR;
    //
    // Do implicit c-n solve for each scalar...but dont reflux.
    // Save the fluxes, coeffs and source term, we need 'em for later
    //
    const int sCompY = first_spec;
    const int nCompY = nspecies;
    MultiFab* alpha = 0; // Allocate lazily
    MultiFab delta_rhs(grids, nCompY, nGrow);
    MultiFab **betan, **betanp1;
    diffusion->allocFluxBoxesLevel(betan  ,nGrow,nCompY);
    diffusion->allocFluxBoxesLevel(betanp1,nGrow,nCompY);
    Array<int> rho_flag(nCompY,0);
    
    MultiFab *alphaSC, *delta_rhsSC, **betanSC, **betanp1SC;

    const MultiFab* Rh = get_rho_half_time();

    for (int sigma = 0; sigma < nCompY; ++sigma)
    {
	const int state_ind = sCompY + sigma;

	// set rho_flag = 2 (diffusion type = laplacian (S/rho))
	// declare alpha, delta_rhs = zero
	// beta_n = D_m^n
	// beta_np1 = D_m^n+1
	diffuse_scalar_setup(dt, state_ind, &rho_flag[sigma], delta_rhsSC,
			     alphaSC, betanSC, betanp1SC);
	
	if (state_ind == sCompY && alphaSC)
	{
	    alpha = new MultiFab(grids, nCompY, nGrow);
	}
	else
	{
	    if ((!alphaSC) ^ !alpha)
		BoxLib::Error("All diff-diffusion must be of same form");
	}   
	//    
	// Nab a copy of the coeffs, call diffuser
	//
	if (alphaSC)
	    MultiFab::Copy(*alpha,*alphaSC,sCompSC,sigma,nCompSC,nGrow);
	
	for (int d=0; d<BL_SPACEDIM; ++d)
	{
	    if (betanSC)
		MultiFab::Copy(  *betan[d],  *betanSC[d],sCompSC,sigma,nCompSC,nGrow);
	    if (betanp1SC)
		MultiFab::Copy(*betanp1[d],*betanp1SC[d],sCompSC,sigma,nCompSC,nGrow);
	}
	//
	// Make sure we've got a place for delta_rhs...predictor will dump any
	// explicit updates taken before this routine into delta_rhs for later.
	//
	if (delta_rhsSC)
	{
	    MultiFab::Copy(delta_rhs,*delta_rhsSC,sCompSC,sigma,nCompSC,nGrow);
	}
	else
	{
	    delta_rhs.setVal(0,sigma,1);
	}
	//
	// Clean up single-component stuff, then diffuse the scalar
	//
	diffuse_cleanup(delta_rhsSC, betanSC, betanp1SC, alphaSC);

	if( state_ind == 4)
	  {
// 	std::cout<<"dt = "<<dt<<std::endl;
// 	std::cout<<"rho_flag = "<<rho_flag[sigma]<<std::endl;
// 	VisMF::Write(delta_rhs,"delta_ht");
// 	VisMF::Write(*betan[0],"betaxn_ht");
// 	VisMF::Write(*betan[1],"betayn_ht");
// 	VisMF::Write(*betanp1[0],"betaxnp1_ht");
// 	VisMF::Write(*betanp1[1],"betaynp1_ht");
	//not built yet
	//VisMF::Write(*alpha,"alpha_ht");
	  }

	diffusion->diffuse_scalar(dt,state_ind,be_cn_theta,Rh,rho_flag[sigma],
                                  fluxSCn,fluxSCnp1,sigma,&delta_rhs,alpha,
                                  betan,betanp1,solve_mode);
	//
	// Pull fluxes into flux array
	//
	for (int d = 0; d < BL_SPACEDIM; ++d)
        {
	    MultiFab::Copy(*SpecDiffusionFluxn[d],  *fluxSCn[d],  sCompSC,sigma,nCompSC,nGrow);
	    MultiFab::Copy(*SpecDiffusionFluxnp1[d],*fluxSCnp1[d],sCompSC,sigma,nCompSC,nGrow);
        }
	spec_diffusion_flux_computed[sigma] = HT_Diffusion;
    }
    diffusion->removeFluxBoxesLevel(fluxSCn);
    diffusion->removeFluxBoxesLevel(fluxSCnp1);

 //      VisMF::Write(*SpecDiffusionFluxnp1[0],"specfluxx_ht");
//       VisMF::Write(*SpecDiffusionFluxnp1[1],"specfluxy_ht");
//       VisMF::Write(*SpecDiffusionFluxn[0],"specfluxxn_ht");
//       VisMF::Write(*SpecDiffusionFluxn[1],"specfluxyn_ht");

    //
    // Modify update/fluxes to preserve flux sum = 0, compute new update and
    // leave modified fluxes in level data.  Do this in two stages, first for
    // the explicit fluxes, then the implicit ones (so send in rhs=0 for the
    // second one...).
    //
    const int  dataComp  = 0;
    const Real prev_time = state[State_Type].prevTime();
    const Real cur_time  = state[State_Type].curTime();

    adjust_spec_diffusion_update(get_new_data(State_Type),&get_old_data(State_Type),
				 sCompY,dt,prev_time,rho_flag,Rh,dataComp,
                                 &delta_rhs,alpha,betan);

    diffusion->removeFluxBoxesLevel(betan);
    delta_rhs.clear();

    adjust_spec_diffusion_update(get_new_data(State_Type),&get_new_data(State_Type),
				 sCompY,dt,cur_time,rho_flag,Rh,dataComp,0,
                                 alpha,betanp1);

    diffusion->removeFluxBoxesLevel(betanp1);
    if (alpha)
	delete alpha;
    //
    // Now do reflux with new, improved fluxes
    //
    if (do_reflux && corrector)
    {
        {
            FArrayBox fluxtot;
            for (int d = 0; d < BL_SPACEDIM; d++)
            {
                for (MFIter fmfi(*SpecDiffusionFluxn[d]); fmfi.isValid(); ++fmfi)
                {
                    const Box& ebox = (*SpecDiffusionFluxn[d])[fmfi].box();

                    fluxtot.resize(ebox,nCompY);
                    fluxtot.copy((*SpecDiffusionFluxn[d])[fmfi], ebox,0,ebox,0,nCompY);
                    fluxtot.plus((*SpecDiffusionFluxnp1[d])[fmfi],ebox,0,0,nCompY);

                    if (level < parent->finestLevel())
                        getLevel(level+1).getViscFluxReg().CrseInit(fluxtot,ebox,d,0,sCompY,nCompY,-dt);

                    if (level > 0)
                        getViscFluxReg().FineAdd(fluxtot,d,fmfi.index(),0,sCompY,nCompY,dt);
                }
            }
        }
	if (level < parent->finestLevel())
	    getLevel(level+1).getViscFluxReg().CrseInitFinish();
    }

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;

    ParallelDescriptor::ReduceRealMax(run_time,IOProc);

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::differential_spec_diffusion_update(): time: " << run_time << std::endl;
}

void
HeatTransfer::make_rho_prev_time ()
{
    const Real prev_time = state[State_Type].prevTime();

    for (FillPatchIterator fpi(*this,*rho_ptime,1,prev_time,State_Type,Density,1);
         fpi.isValid();
         ++fpi)
    {
        (*rho_ptime)[fpi.index()].copy(fpi());
    }
}

void
HeatTransfer::make_rho_curr_time ()
{
    const Real curr_time = state[State_Type].curTime();

    for (FillPatchIterator fpi(*this,*rho_ctime,1,curr_time,State_Type,Density,1);
         fpi.isValid();
         ++fpi)
    {
        (*rho_ctime)[fpi.index()].copy(fpi());
    }
}

void
HeatTransfer::adjust_spec_diffusion_update (MultiFab&              Phi_new,
					    const MultiFab*        Phi_old,
					    int                    sCompS,
					    Real                   dt,
					    Real                   time,
					    const Array<int>&      rho_flag,
					    const MultiFab*        rho_half,
					    int                    dataComp,
					    const MultiFab*        delta_rhs, 
					    const MultiFab*        alpha, 
					    const MultiFab* const* betanp1)
{
    //
    // Here, we're going to compute an update using fluxes computed from
    // the old state and a guess for the new one.  These fluxes are modified
    // arbitrarily however to preserve that the sum over all species of the
    // diffusive fluxes is zero.  We do this by marching though each edge 
    // and adjusting the flux of the downstream state that is the "dominant"
    // species.  Improved fluxes are left in SpecDiffusionFlux.
    //
    BL_ASSERT(!alpha ^ (alpha&&alpha->nComp()  == nspecies));
    BL_ASSERT(betanp1 && betanp1[0]->nComp()   == nspecies);
    BL_ASSERT(!delta_rhs || delta_rhs->nComp() == nspecies);

    const TimeLevel whichTime = which_time(State_Type,time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);
    
    MultiFab* const* flux = (whichTime == AmrOldTime) ? SpecDiffusionFluxn : SpecDiffusionFluxnp1;

    const int nGrowOp = 1;
    MultiFab rho_and_species(grids,nspecies+1,nGrowOp);
    //
    // Create and fill a full MultiFab of all species at this level and below.
    //
    {
        FArrayBox tmp;

        for (FillPatchIterator fpi(*this,rho_and_species,nGrowOp,time,State_Type,Density,nspecies+1);
             fpi.isValid();
             ++fpi)
        {
            FArrayBox& rho_and_spec = rho_and_species[fpi];

            rho_and_spec.copy(fpi(),0,0,nspecies+1);

            tmp.resize(rho_and_spec.box(),1);
            tmp.copy(rho_and_spec,0,0,1);
            tmp.invert(1);

            for (int comp = 0; comp < nspecies; ++comp) 
                if (rho_flag[comp] == 2)
                    rho_and_spec.mult(tmp,0,comp+1,1);
        }
    }

    MultiFab rho_and_species_crse;

    if (level > 0) 
    {
        const int     nGrow   = 1;
        HeatTransfer& coarser = *(HeatTransfer*) &(parent->getLevel(level-1));

        rho_and_species_crse.define(coarser.grids,nspecies+1,nGrowOp,Fab_allocate);

        FArrayBox tmp;

        for (FillPatchIterator fpi(coarser,rho_and_species_crse,nGrow,time,State_Type,Density,nspecies+1);
             fpi.isValid();
             ++fpi)
        {
            FArrayBox& fab = rho_and_species_crse[fpi];

            fab.copy(fpi(),0,0,nspecies+1);

            tmp.resize(fab.box(),1);
            tmp.copy(fab,0,0,1);
            tmp.invert(1);

            for (int comp = 0; comp < nspecies; ++comp) 
                if (rho_flag[comp] == 2)
                    fab.mult(tmp,0,comp+1,1);
        }
    }

    const Real a = 1.0;
    const Real b = -dt;

    for (int comp = 0; comp < nspecies; ++comp)
    {
	const int state_ind = first_spec + comp;

        ViscBndry  visc_bndry;
        diffusion->getBndryDataGivenS(visc_bndry,rho_and_species,rho_and_species_crse,
                                      state_ind,comp+1,1,time,rho_flag[comp]);
        bool bndry_already_filled = true;

	Real           rhsscale;
        ABecLaplacian* visc_op;
	visc_op = diffusion->getViscOp(state_ind,a,b,time,visc_bndry,
                                       rho_half,rho_flag[comp],&rhsscale,
                                       dataComp+comp,betanp1,alpha,bndry_already_filled);
	visc_op->maxOrder(diffusion->maxOrder());

	rho_and_species.setBndry(bogus_value,comp+1,1); // Ensure computable corners

	visc_op->applyBC(rho_and_species,comp+1,1);

	delete visc_op;

	if (rho_flag[comp] == 2)
        {
	    for (MFIter Smfi(rho_and_species); Smfi.isValid(); ++Smfi)
            {
                FArrayBox& fab = rho_and_species[Smfi];
     		fab.mult(fab,fab.box(),0,comp+1,1);
            }
        }
    }

    rho_and_species_crse.clear();    
    //
    // "Repair" fluxes to ensure they sum to zero, use rho_and_species to pick dominant comp
    //  Note that rho_and_species is now rho and rho.Y, not rho and Y.
    //
    for (MFIter mfi(rho_and_species); mfi.isValid(); ++mfi)
    {
        FArrayBox& state = rho_and_species[mfi];
        const Box& box   = mfi.validbox();

        for (int d =0; d < BL_SPACEDIM; ++d)
        {
            FArrayBox& fab = (*flux[d])[mfi.index()];
            FORT_REPAIR_FLUX(box.loVect(), box.hiVect(),
                             fab.dataPtr(),  ARLIM(fab.loVect()),  ARLIM(fab.hiVect()),
                             state.dataPtr(1),ARLIM(state.loVect()),ARLIM(state.hiVect()),
                             &nspecies, &d);
        }
    }
    rho_and_species.clear();
    //
    // Reset Phi_new using "repaired" fluxes.  Here, we assume the following
    // arrangement of terms:
    //
    //     Phi_new = Phi_old + (dt/A)(RHS-Div(flux))
    //            A = rhonph.alpha for rho_flag == 1
    //            A = 1            for rho_flag == 2
    //     
    //
    FArrayBox update, tmp, volume;

    for (MFIter mfi(Phi_new); mfi.isValid(); ++mfi)
    {
	int        iGrid = mfi.index();
	const Box& box   = mfi.validbox();
	//
	// Do same trick as in Diffusion class, so as to deal with r-z correctly
	// (i.e. multiply though by cell-centered r.dx.dz [integrating over vol].
	//  Since fluxes are already extensive, merely mult flux by dx, then scale
	//  flux divergence by vol...that should do it)
	//
	// Here then, the update becomes: (dt/A)[RHS - (1/V)[Div(flux.dx)]]
	//
	// In RECOMP_UPDATE, we compute (-1/V).Div(flux.dx)
        //
        // Also, by putting update into a separate fab, and copying into afterward
        // we allow that Phi_old = &Phi_new, i.e. that we are updating an existing
        // state
	//
        update.resize(box,nspecies);
        geom.GetVolume(volume,grids,iGrid,GEOM_GROW);
	FORT_RECOMP_UPDATE(box.loVect(), box.hiVect(),
			   update.dataPtr(),
			   ARLIM(update.loVect()),       ARLIM(update.hiVect()),
			   (*flux[0])[iGrid].dataPtr(),
                           ARLIM((*flux[0])[iGrid].loVect()),
                           ARLIM((*flux[0])[iGrid].hiVect()),
			   (*flux[1])[iGrid].dataPtr(),
			   ARLIM((*flux[1])[iGrid].loVect()),
                           ARLIM((*flux[1])[iGrid].hiVect()),
#if BL_SPACEDIM == 3
			   (*flux[2])[iGrid].dataPtr(),
			   ARLIM((*flux[2])[iGrid].loVect()),
                           ARLIM((*flux[2])[iGrid].hiVect()),
#endif
			   volume.dataPtr(),
			   ARLIM(volume.loVect()),ARLIM(volume.hiVect()),
			   &nspecies);
	
	if (delta_rhs)
	    update.plus((*delta_rhs)[iGrid],box,dataComp,0,nspecies);

	update.mult(dt,box,0,nspecies);

	if (alpha)
	    update.divide((*alpha)[iGrid],box,dataComp,0,nspecies);

        tmp.resize(box,1);
        tmp.copy((*rho_half)[iGrid],0,0,1);
        tmp.invert(1);

	for (int i = 0; i < nspecies; ++i)
	    if (rho_flag[i] == 1)
		update.mult(tmp,0,i,1);

	if (Phi_old)
	    update.plus((*Phi_old)[iGrid],box,sCompS,0,nspecies);

        Phi_new[mfi].copy(update,box,0,box,sCompS,nspecies);
    }
}

void
HeatTransfer::diffuse_scalar_setup (Real        dt,
                                    int         sigma,
                                    int*        rho_flag, 
                                    MultiFab*&  delta_rhs,
                                    MultiFab*&  alpha, 
                                    MultiFab**& betan,
                                    MultiFab**& betanp1)
{
    //
    // Do setup for implicit c-n solve for an arbitrary scalar.
    //
    // Note: should be ok for variable c_p.
    //
    const Real prev_time = state[State_Type].prevTime();

    // this function only executes one line of code.
    // to set rho_flag
    // is this really neccessary?
    NavierStokes::diffuse_scalar_setup(dt, sigma, rho_flag, 
                                       delta_rhs, alpha, betan, betanp1);
    alpha     = 0;
    delta_rhs = 0;
    betan     = 0;
    betanp1   = 0;
   
    if (sigma == Temp)
    {
      //diffusion type = (laplacian S)/rho
        (*rho_flag) = 1;
    }
    else if (sigma == RhoH || sigma >= first_spec && sigma <= last_spec)
    {
      // diffusion type = laplacian(S/rho)
        (*rho_flag) = 2;
    }

    if (sigma == RhoH)
    {
        diffuse_rhoh_setup(prev_time,dt,delta_rhs); 
    }
    else if (sigma == Temp)
    {
        diffuse_temp_setup(prev_time,dt,delta_rhs,alpha); 
    }
    else if (sigma >= first_spec && sigma <= last_spec)
    {
        diffuse_spec_setup(sigma,prev_time,dt,delta_rhs); 
    }

    diffusion->allocFluxBoxesLevel(betan);
    diffusion->allocFluxBoxesLevel(betanp1);
    getDiffusivity(betan, prev_time, sigma, 0, 1);
    getDiffusivity(betanp1, prev_time+dt, sigma, 0, 1);
}

void
HeatTransfer::diffuse_spec_setup (int        istate,
                                  Real       time,
                                  Real       dt, 
                                  MultiFab*& delta_rhs)
{
    //
    // Chemistry split, no source terms
    //
    delta_rhs = new MultiFab(grids,1,0);
    delta_rhs->setVal(0);
}

void
HeatTransfer::compute_OT_radloss (Real      time,
                                  int       nGrow,
                                  MultiFab& dqrad)
{
    //
    // Get optically thin radiation losses (+ve when energy LOST).
    //
    BL_ASSERT(do_OT_radiation || do_heat_sink);
    BL_ASSERT(nGrow <= dqrad.nGrow());
    BL_ASSERT(dqrad.boxArray() == grids);

    const Real* dx = geom.CellSize();

    Real p_amb, dpdt_factor;
    FORT_GETPAMB(&p_amb, &dpdt_factor);

    const Real Patm = p_amb / P1atm_MKS;
    const Real T_bg = 298.0;

    for (FillPatchIterator fpi(*this,dqrad,nGrow,time,State_Type,0,NUM_STATE);
         fpi.isValid();
         ++fpi)
    {
        FArrayBox& S   = fpi();
        const Box& box = S.box();

        {
            FArrayBox tmp(box,1);
            tmp.copy(S,Density,0,1);
            tmp.invert(1);
            for (int spec = first_spec; spec <= last_spec; spec++)
                S.mult(tmp,0,spec,1);
        }

        FArrayBox& dqrad_fab = dqrad[fpi];

        {
            FArrayBox X(box,nspecies);
            getChemSolve().massFracToMoleFrac(X,S,box,first_spec,0);

            if (do_OT_radiation)
            {
                getChemSolve().getOTradLoss_TDF(dqrad_fab,S,X,Patm,T_bg,box,0,Temp,0);
            }
            else
            {
                dqrad_fab.setVal(0.0);
            }
        }
        //
        // Add arbitrary heat sink.
        //
        if (do_heat_sink)
        {
            FArrayBox rad(box,1);
            FORT_RADLOSS(box.loVect(),box.hiVect(),
                         rad.dataPtr(),         ARLIM(rad.loVect()),ARLIM(rad.hiVect()),
                         S.dataPtr(Temp),       ARLIM(S.loVect()),  ARLIM(S.hiVect()),
                         S.dataPtr(first_spec), ARLIM(S.loVect()),  ARLIM(S.hiVect()),
                         dx, &Patm, &time);
            dqrad_fab.plus(rad,0,0,1);
        }
    }
}

void
HeatTransfer::diffuse_rhoh_setup (Real       time,
                                  Real       dt, 
                                  MultiFab*& delta_rhs)
{
    //
    // Do set-up for implicit c-n solve for rho*h using Le=1 equation.
    //
    BL_ASSERT(delta_rhs==0);
    delta_rhs = new MultiFab (grids,1,0);
    delta_rhs->setVal(0);
    const int nGrow = 0;

    if (do_OT_radiation || do_heat_sink)
    {
        MultiFab dqrad(grids,1,nGrow);

        compute_OT_radloss(time,nGrow,dqrad);
        for (MFIter mfi(dqrad); mfi.isValid(); ++mfi)
        {
            dqrad[mfi].mult(1.0-be_cn_theta);
            (*delta_rhs)[mfi].minus(dqrad[mfi]);
        }

        compute_OT_radloss(time+dt,nGrow,dqrad);
        for (MFIter mfi(dqrad); mfi.isValid(); ++mfi)
        {
            dqrad[mfi].mult(be_cn_theta);
            (*delta_rhs)[mfi].minus(dqrad[mfi]);
        }
    }
}

void
HeatTransfer::diffuse_temp_setup (Real       prev_time,
                                  Real       dt, 
                                  MultiFab*& delta_rhs,
                                  MultiFab*& alpha)
{
    //
    // Do set-up for implicit c-n solve for T.
    //
    BL_ASSERT(delta_rhs==0);
    delta_rhs = new MultiFab (grids,1,0);
    delta_rhs->setVal(0);
    const int nGrow = 0;

    if (do_OT_radiation || do_heat_sink)
    {
        MultiFab dqrad(grids,1,nGrow);

        compute_OT_radloss(prev_time,nGrow,dqrad);
        for (MFIter mfi(dqrad); mfi.isValid(); ++mfi)
        {
            dqrad[mfi].mult(1.0-be_cn_theta);
            (*delta_rhs)[mfi].minus(dqrad[mfi]);
        }

        compute_OT_radloss(prev_time+dt,nGrow,dqrad);
        for (MFIter mfi(dqrad); mfi.isValid(); ++mfi)
        {
            dqrad[mfi].mult(be_cn_theta);
            (*delta_rhs)[mfi].minus(dqrad[mfi]);
        }
    }
    //
    // Increment rhs by (+ sum_l rho D grad Y_l dot grad h_l)
    // Note: this way ensures isothermal preservation
    //
    MultiFab rdgydgh(grids,1,0);
    compute_rhoDgradYgradH(prev_time, rdgydgh);
    rdgydgh.mult(1.0-be_cn_theta,0);
    
    for (MFIter mfi(*delta_rhs); mfi.isValid(); ++mfi)
    {
        (*delta_rhs)[mfi].plus(rdgydgh[mfi],0,0,1);
    }
    compute_rhoDgradYgradH(prev_time+dt, rdgydgh);
    rdgydgh.mult(be_cn_theta,0);
    
    for (MFIter mfi(*delta_rhs); mfi.isValid(); ++mfi)
    {
        (*delta_rhs)[mfi].plus(rdgydgh[mfi],0,0,1);
    }
    rdgydgh.clear();
    //
    // alpha = c_p^(n+1/2)
    // Note: rho accounted for in diffusion box
    //
    BL_ASSERT(alpha==0);
    alpha = new MultiFab (grids,1,0);
    compute_cp(prev_time+dt/2,*alpha);
}

void
HeatTransfer::diffuse_cleanup (MultiFab*&  delta_rhs,
                               MultiFab**& betan,
                               MultiFab**& betanp1,
                               MultiFab*&  alpha)
{
    delete delta_rhs;
    delete alpha;
    alpha = delta_rhs = 0;

    diffusion->removeFluxBoxesLevel(betan);
    diffusion->removeFluxBoxesLevel(betanp1);
}

void
HeatTransfer::diffuse_cleanup (MultiFab*&  delta_rhs,
                               MultiFab**& betan,
                               MultiFab**& betanp1)
{
    MultiFab* alpha = 0;
    diffuse_cleanup(delta_rhs, betan, betanp1, alpha);
}

void
HeatTransfer::velocity_diffusion_update (Real dt)
{
    //
    // Do implicit c-n solve for velocity
    // compute the viscous forcing
    // do following except at initial iteration--rbp, per jbb
     //
    if (is_diffusive[Xvel])
    {
        int rho_flag;
        if (do_mom_diff == 0)
        {
           rho_flag = 1;
        }
        else
        {
           rho_flag = 3;
        }

        MultiFab *delta_rhs = 0, **betan = 0, **betanp1 = 0;

        diffuse_velocity_setup(dt, delta_rhs, betan, betanp1);

        diffusion->diffuse_velocity(dt,be_cn_theta,get_rho_half_time(),rho_flag,
                                    delta_rhs,betan,betanp1);

        diffuse_cleanup(delta_rhs, betan, betanp1);
    }
}
    
void
HeatTransfer::diffuse_velocity_setup (Real        dt,
                                      MultiFab*&  delta_rhs,
                                      MultiFab**& betan,
                                      MultiFab**& betanp1)
{
    //
    // Do setup for implicit c-n solve for velocity.
    //
    BL_ASSERT(delta_rhs==0);
    BL_ASSERT(betan==0);
    BL_ASSERT(betanp1==0);
    const Real time = state[State_Type].prevTime();
    //
    // Assume always variable viscosity.
    //
    diffusion->allocFluxBoxesLevel(betan);
    diffusion->allocFluxBoxesLevel(betanp1);
    
    getViscosity(betan, time);
    getViscosity(betanp1, time+dt);
    //
    // Do the following only if it is a non-trivial operation.
    //
    if (S_in_vel_diffusion)
    {
        //
        // Include div mu S*I terms in rhs
        //  (i.e. make nonzero delta_rhs to add into RHS):
        //
        // The scalar and tensor solvers incorporate the relevant pieces of
        //  of Div(tau), provided the flow is divergence-free.  Howeever, if
        //  Div(U) =/= 0, there is an additional piece not accounted for,
        //  which is of the form A.Div(U).  For constant viscosity, Div(tau)_i
        //  = Lapacian(U_i) + mu/3 d[Div(U)]/dx_i.  For mu not constant,
        //  Div(tau)_i = d[ mu(du_i/dx_j + du_j/dx_i) ]/dx_i - 2mu/3 d[Div(U)]/dx_i
        //
        // As a convenience, we treat this additional term as a "source" in
        // the diffusive solve, computing Div(U) in the "normal" way we
        // always do--via a call to calc_divu.  This routine computes delta_rhs
        // if necessary, and stores it as an auxilliary rhs to the viscous solves.
        // This is a little strange, but probably not bad.
        //
        delta_rhs = new MultiFab(grids,BL_SPACEDIM,0);
        delta_rhs->setVal(0);
      
        MultiFab divmusi(grids,BL_SPACEDIM,0);
        //
	// Assume always variable viscosity.
        //
	diffusion->compute_divmusi(time,betan,divmusi);
	divmusi.mult((-2./3.)*(1.0-be_cn_theta),0,BL_SPACEDIM,0);
        (*delta_rhs).plus(divmusi,0,BL_SPACEDIM,0);
        //
	// Assume always variable viscosity.
        //
	diffusion->compute_divmusi(time+dt,betanp1,divmusi);
	divmusi.mult((-2./3.)*be_cn_theta,0,BL_SPACEDIM,0);
        (*delta_rhs).plus(divmusi,0,BL_SPACEDIM,0);
    }
}

void
HeatTransfer::getViscTerms (MultiFab& visc_terms,
                            int       src_comp, 
                            int       num_comp,
                            Real      time)
{
    //
    // Load "viscous" terms, starting from component = 0.
    //
    // JFG: for species, this procedure returns the *negative* of the divergence of 
    // of the diffusive fluxes.  specifically, in the mixture averaged case, the
    // diffusive flux vector for species k is
    //
    //       j_k = - rho D_k,mix grad Y_k
    //
    // so the divergence of the flux, div dot j_k, has a negative in it.  instead 
    // this procedure returns - div dot j_k to remove the negative.
    //
    // note the fluxes used in the code are extensive, that is, scaled by the areas
    // of the cell edges.  the calculation of the divergence is the sum of un-divided
    // differences of the extensive fluxes, all divided by volume.  so the effect is 
    // to give the true divided difference approximation to the divergence of the
    // intensive flux.
    //
    const int  last_comp = src_comp + num_comp - 1;
    int        icomp     = src_comp; // This is the current related state comp.
    int        load_comp = 0;        // Comp for result of current calculation.
    MultiFab** vel_visc  = 0;        // Potentially reused, raise scope
    const int  nGrow     = visc_terms.nGrow();
    //
    // Get Div(tau) from the tensor operator, if velocity and have non-const viscosity
    //
    if (src_comp < BL_SPACEDIM)
    {
        if (src_comp != Xvel || num_comp < BL_SPACEDIM)
            BoxLib::Error("tensor v -> getViscTerms needs all v-components at once");

        diffusion->allocFluxBoxesLevel(vel_visc);
        getViscosity(vel_visc, time);
        diffusion->getTensorViscTerms(visc_terms,time,0,vel_visc);
        icomp = load_comp = BL_SPACEDIM;
    }
    //
    // For Le != 1, need to get visc terms in a block
    //
    if (!unity_Le)
    {
	const int non_spec_comps = std::min(num_comp,
                                            std::max(0,first_spec-src_comp) + std::max(0,last_comp-last_spec));
	const bool has_spec = src_comp <= last_spec && last_comp >= first_spec;
	BL_ASSERT( !has_spec || (num_comp>=nspecies+non_spec_comps));
	if (has_spec)
	{
	    const int sCompY = first_spec - src_comp + load_comp;
            const int sCompT = Temp - src_comp + load_comp;
            if (do_mcdd)
            {
		// code for Marc's multicomponent diffusion

                BL_ASSERT(sCompT > 0 && sCompT < visc_terms.nComp());
                compute_mcdd_visc_terms(visc_terms,sCompY,visc_terms,sCompT,time,nGrow,DDOp::DD_Temp);
            }
            else if (do_rk_diffusion)
	    {
		// code for Joe's Runge-Kutta diffusion

		// apply the diffusion operator to the old or new state to get updates
		// associated with that state.  only the species divergences are used here, 
		// but space is still needed for the others.
		MultiFab* div_of_flux_for_H;
		MultiFab* div_of_flux_for_Y;
		MultiFab** flux_for_H;
		MultiFab** flux_for_Y;

		// per the comments above, pass - 1.0 as the scaling argument.
		rk_diffusion_operator (time,
				       - 1.0,
				       div_of_flux_for_H,
				       div_of_flux_for_Y,
				       flux_for_H,
				       flux_for_Y);

		// examination of visc_terms.nGrow() reveals getViscTerms is called twice,
		// once with 1 growth cell, and once with 0.  the original implementation,
		// in the following else block, compute_differential_diffusion_terms 
                // contains a remark that it does not fill growth cells, so we also do
		// not fill them here.  note the arguments for MultiFab:: procedures are 
		// (dst, src, srccomp, dstcomp, ncomp, nghost);
		MultiFab::Copy (visc_terms, *div_of_flux_for_Y, 0, sCompY, nspecies, 0);

		// delete the space for fluxes and updates
		delete div_of_flux_for_H;
		delete div_of_flux_for_Y;
		diffusion->removeFluxBoxesLevel (flux_for_H);
		diffusion->removeFluxBoxesLevel (flux_for_Y);
	    }
	    else 
	    {
	      // code for the original implementation
	      //
	      // CEG: as far as I can tell, this just gets the diffusion 
	      // terms for the species (it computes the fluxes first, adjusts
	      // to conserve mass by subracting from dominate species, and
	      // then takes the divergence)
		compute_differential_diffusion_terms(visc_terms,sCompY,time);
		//		VisMF::Write(visc_terms,"orig_diff");
    
            }
        }
    }
    //
    // Now, do the rest.
    // Get Div(visc.Grad(state)) or Div(visc.Grad(state/rho)), as appropriate.
    //
    for ( ; icomp <= last_comp; load_comp++, icomp++)
    {
	const bool is_spec  = icomp >= first_spec && icomp <= last_spec;
	if ( !(is_spec && !unity_Le) )
	{
	    if (icomp == Temp)
	    {
                if (do_mcdd) 
		{
		    // Do nothing, because in this case, was done above
		}
		else if (do_rk_diffusion)
		{
		    // code for Joe's Runge-Kutta diffusion
		    
		    // apply the diffusion operator to the old or new state to get updates
		    // associated with that state.  only the species divergences are used here, 
		    // but space is still needed for the others.
		    MultiFab* div_of_flux_for_H;
		    MultiFab* div_of_flux_for_Y;
		    MultiFab** flux_for_H;
		    MultiFab** flux_for_Y;
		    
		    // per the comments above, pass - 1.0 as the scaling argument.
		    rk_diffusion_operator (time,
					   - 1.0,
					   div_of_flux_for_H,
					   div_of_flux_for_Y,
					   flux_for_H,
					   flux_for_Y);
		    
		    MultiFab& S_old = get_old_data (State_Type);
		    FArrayBox cpmix, h, Y;
		    for (MFIter mfi (S_old); mfi.isValid (); ++mfi)
		    {
			const Box& box = mfi.validbox ();
			// form Y
			Y.resize (box, nspecies);
			Y.copy (S_old[mfi], first_spec, 0, nspecies);
			for (int k = 0; k < nspecies; ++k)
			    Y.divide (S_old[mfi], Density, k, 1);
			// form cpmix
			cpmix.resize (box, 1);
			getChemSolve().getCpmixGivenTY (cpmix, S_old[mfi], Y, box, Temp, 0, 0);
			// form h_k
			h.resize (box, nspecies);
			getChemSolve ().getHGivenT(h, S_old[mfi], box, Temp, 0);
			// assemble the right side of the temperature equation
			// note the minus sign has been included in the scaling of the divergences
			visc_terms[mfi].copy ((*div_of_flux_for_H)[mfi]);
			h.mult ((*div_of_flux_for_Y)[mfi], 0, 0, nspecies);
			for (int k = 0; k < nspecies; ++k)
			    visc_terms[mfi].plus (h, k, 0, 1);
			visc_terms[mfi].divide (cpmix);
		    }
		    // delete the space for fluxes and updates
		    delete div_of_flux_for_H;
		    delete div_of_flux_for_Y;
		    diffusion->removeFluxBoxesLevel (flux_for_H);
		    diffusion->removeFluxBoxesLevel (flux_for_Y);
		}
		else
		{
		    // code for the original implementation

                    getTempViscTerms(visc_terms,Temp-load_comp,time);
		}
	    }
	    else if (icomp == RhoH)
	    {
	      if (do_mcdd)
                    // What to do here?  
                    BoxLib::Abort("do we really want to get RhoH VT when do_mcdd?");
	    }
	    else
	    {
		const int  rho_flag = Diffusion::set_rho_flag(diffusionType[icomp]);
		MultiFab** beta     = 0;

		if (icomp == Density)
                {
                    visc_terms.setVal(0.0,load_comp,1);
                }
                else
                {
		    // JFG: can a return be placed here for rk_diffusion?
                    //
                    // Assume always variable viscosity / diffusivity.
                    //
                    diffusion->allocFluxBoxesLevel(beta);
                    getDiffusivity(beta, time, icomp, 0, 1);

                    diffusion->getViscTerms(visc_terms,icomp-load_comp,
                                            icomp,time,rho_flag,0,beta);
                    
                    diffusion->removeFluxBoxesLevel(beta);
                }
	    }
	}
    }
    //
    // Add Div(u) term if desired, if this is velocity, and if Div(u) is nonzero
    // If const-visc, term is mu.Div(u)/3, else it's -Div(mu.Div(u).I)*2/3
    //
    if (src_comp < BL_SPACEDIM && S_in_vel_diffusion)
    {
        if (num_comp < BL_SPACEDIM)
            BoxLib::Error("getViscTerms() need all v-components at once");
    
        MultiFab divmusi(grids,BL_SPACEDIM,1);
        //
	// Assume always using variable viscosity.
        //
	diffusion->compute_divmusi(time,vel_visc,divmusi); // pre-computed visc above
	divmusi.mult((-2./3.),0,BL_SPACEDIM,0);
        visc_terms.plus(divmusi,Xvel,BL_SPACEDIM,0);
    }
    //
    // Clean up your mess ...
    //
    if (vel_visc)
        diffusion->removeFluxBoxesLevel(vel_visc);
    //
    // Ensure consistent grow cells
    //    
    if (nGrow > 0)
    {
        for (MFIter mfi(visc_terms); mfi.isValid(); ++mfi)
        {
            FArrayBox& vt  = visc_terms[mfi];
            const Box& box = mfi.validbox();
            FORT_VISCEXTRAP(vt.dataPtr(),ARLIM(vt.loVect()),ARLIM(vt.hiVect()),
                            box.loVect(),box.hiVect(),&num_comp);
        }
        visc_terms.FillBoundary(0,num_comp);
        //
        // Note: this is a special periodic fill in that we want to
        // preserve the extrapolated grow values when periodic --
        // usually we preserve only valid data.  The scheme relies on
        // the fact that there is good data in the "non-periodic" grow cells.
        // ("good" data produced via VISCEXTRAP above)
        //
        geom.FillPeriodicBoundary(visc_terms,0,num_comp,true);
    }
}

void
HeatTransfer::fill_mcdd_boundary_data(Real time)
{
    const int nGrow = LinOp_grow;
    MultiFab S(grids,nspecies+1,nGrow);

    // Fill temp mf with Y,T
    const int compT = nspecies;
    const int compY = 0;

    for (FillPatchIterator T_fpi(*this,S,nGrow,time,State_Type,Temp,1),
             Y_fpi(*this,S,nGrow,time,State_Type,first_spec,nspecies),
             Rho_fpi(*this,S,nGrow,time,State_Type,Density,1);
         T_fpi.isValid() && Y_fpi.isValid() && Rho_fpi.isValid();
          ++T_fpi, ++Y_fpi, ++Rho_fpi)
    {
        // HACK: Assumes all fill-patched boundary data is coincident
        FArrayBox&       Sfab = S[T_fpi];
        const FArrayBox& Yfab = Y_fpi();
        const FArrayBox& Tfab = T_fpi();
        const FArrayBox& Rfab = Rho_fpi();

        Sfab.copy(Yfab,0,compY,nspecies);
        Sfab.copy(Tfab,0,compT,1);

        for (int n=0; n<nspecies; ++n)
            Sfab.divide(Rfab,0,compY+n,1);
    }
    
    BndryRegister* cbr = 0;
    const int compCT = nspecies;
    const int compCY = 0;

    if (level != 0)
    {
        BoxArray cgrids = BoxArray(grids).coarsen(crse_ratio);
        cbr = new BndryRegister();
        cbr->setBoxes(cgrids);
        for (OrientationIter fi; fi; ++fi)
            cbr->define(fi(),IndexType::TheCellType(),0,1,1,nspecies+1);

        const int nGrowC = 1;
        MultiFab SC(cgrids,nspecies+1,nGrowC,Fab_allocate);

        FillPatchIterator TC_fpi(parent->getLevel(level-1),SC,nGrowC,time,
                                 State_Type,Temp,1);
        FillPatchIterator YC_fpi(parent->getLevel(level-1),SC,nGrowC,time,
                                 State_Type,first_spec,nspecies);
        FillPatchIterator RhoC_fpi(parent->getLevel(level-1),SC,nGrowC,time,
                                   State_Type,Density,1);
        for ( ; TC_fpi.isValid() && YC_fpi.isValid() && RhoC_fpi.isValid();
              ++TC_fpi, ++YC_fpi, ++RhoC_fpi)
        {

            FArrayBox&       SCfab = SC[TC_fpi];
            const FArrayBox& YCfab = YC_fpi();
            const FArrayBox& TCfab = TC_fpi();
            const FArrayBox& RCfab = RhoC_fpi();

            SCfab.copy(YCfab,0,compY,nspecies);
            SCfab.copy(TCfab,0,compT,1);

            for (int n=0; n<nspecies; ++n)
                SCfab.divide(RCfab,0,compY+n,1);
        }

        cbr->copyFrom(SC,nGrowC,compY,compCY,nspecies);
        cbr->copyFrom(SC,nGrowC,compT,compCT,1);
    }

    MCDDOp.setBoundaryData(S,compT,S,compY,cbr,compCT,cbr,compCY,
                           get_desc_lst()[State_Type].getBC(Temp),
                           get_desc_lst()[State_Type].getBC(first_spec));
}

void
HeatTransfer::compute_mcdd_visc_terms(MultiFab&           vtermsY,
                                      int                 dCompY,
                                      MultiFab&           vtermsH,
                                      int                 dCompH,
                                      Real                time,
                                      int                 nGrow,
                                      DDOp::DD_ApForTorRH whichApp,
                                      PArray<MultiFab>*   flux,
                                      int                 dCompFY,
                                      int                 dCompFH)
{
    if (hack_nospecdiff)
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "... HACK!!! zeroing spec+enth diffusion terms " << '\n';
        vtermsY.setVal(0.0,dCompY,nspecies,nGrow);
        vtermsH.setVal(0.0,dCompH,1,nGrow);
        return;            
    }

    BL_ASSERT(vtermsY.boxArray()==grids);
    BL_ASSERT(vtermsH.boxArray()==grids);
    BL_ASSERT(vtermsY.nComp() >= dCompY+nspecies);
    BL_ASSERT(vtermsH.nComp() > dCompH);
    
    // If we didn't get space to write fluxes, make some locally
    PArray<MultiFab>* tFlux;
    PArray<MultiFab> fluxLocal(BL_SPACEDIM,PArrayManage);
    if (flux==0)
    {
        for (int dir=0; dir<BL_SPACEDIM; ++dir)
            fluxLocal.set(dir,new MultiFab(BoxArray(grids).surroundingNodes(dir),nspecies+1,0));
        tFlux = &fluxLocal;
        dCompFY = 0;
        dCompFH = nspecies;
    }
    else
    {
       tFlux = flux;
    }
 
    // Load boundary data in operator (using c-f data where required)
    fill_mcdd_boundary_data(time);

    const int sCompY = 0;
    const int sCompT = nspecies;
    const int nGrowOp = 1;
    MultiFab S(grids,nspecies+1,nGrowOp);
    const MultiFab& State = get_data(State_Type,time);
    MultiFab::Copy(S,State,first_spec,sCompY,nspecies,0);
    MultiFab::Copy(S,State,Temp,sCompT,1,0);
    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        FArrayBox&           Sfab = S[mfi];
        const FArrayBox& StateFab = State[mfi];
        const Box& box = mfi.validbox();
        for (int i=0; i<nspecies; ++i)
            Sfab.divide(StateFab,box,Density,sCompY+i,1);
    }

    // If whichVT = HT_Temp, do not add hi.rho.Yi.Vi to fluxes and result
    // Also, add -(grad(T).sum(cpi.gammai))/cpmix
    MCDDOp.applyOp(vtermsH,dCompH,vtermsY,dCompY,S,sCompT,S,sCompY,
                   *tFlux,dCompFH,*tFlux,dCompFY,whichApp);
    //
    // Add heat source terms to RhoH component
    //
    add_heat_sources(vtermsH,dCompH,time,nGrow,1.0);
    //
    // Ensure consistent grow cells
    //    
    if (nGrow > 0)
    {
        const int ncH = 1;
        for (MFIter mfi(vtermsY); mfi.isValid(); ++mfi)
        {
            FArrayBox& vtY = vtermsY[mfi];
            const Box& box = mfi.validbox();
            FORT_VISCEXTRAP(vtY.dataPtr(dCompY),ARLIM(vtY.loVect()),ARLIM(vtY.hiVect()),
                            box.loVect(),box.hiVect(),&nspecies);

            FArrayBox& vtH = vtermsH[mfi];
            FORT_VISCEXTRAP(vtH.dataPtr(dCompH),ARLIM(vtH.loVect()),ARLIM(vtH.hiVect()),
                            box.loVect(),box.hiVect(),&ncH);
        }
        vtermsY.FillBoundary(dCompY,nspecies);
        vtermsH.FillBoundary(dCompH,1);
        //
        // Note: this is a special periodic fill in that we want to
        // preserve the extrapolated grow values when periodic --
        // usually we preserve only valid data.  The scheme relies on
        // the fact that there is computable data in the "non-periodic"
        // grow cells (produced via VISCEXTRAP above)
        //
        geom.FillPeriodicBoundary(vtermsY,dCompY,nspecies,true);
        geom.FillPeriodicBoundary(vtermsH,dCompH,1,true);
    }
}

Real MFnorm(const MultiFab& mf,
            int             sComp,
            int             nComp)
{
    const int p = 0; // max norm
    Real normtot = 0.0;
    for (MFIter mfi(mf); mfi.isValid(); ++mfi)
        normtot = std::max(normtot,mf[mfi].norm(mfi.validbox(),p,sComp,nComp));
    ParallelDescriptor::ReduceRealMax(normtot);
    return normtot;
}

void
HeatTransfer::compute_mcdd_relax_parameter(MultiFab&       lambda,
                                           int             dCompY,
                                           int             dCompH,
                                           const MultiFab& Y,
                                           int             sCompY,
                                           const MultiFab& T,
                                           int             sCompT,
                                           const MultiFab& Rho,
                                           int             sCompR,
                                           Real            dt,
                                           int             mgLevel)
{
    Real dx_min = geom.CellSize(0);
    for (int d=1; d<BL_SPACEDIM; ++d)
        dx_min = std::min(dx_min,geom.CellSize(d));
    dx_min *= pow(float(2),float(mgLevel));

    lambda.setVal(0.);
    MCDDOp.setRelax(lambda,dCompH,T,sCompT,Y,sCompY,mgLevel);
    Real scale = be_cn_theta*dt/(dx_min*dx_min);
    lambda.setVal(mcdd_relaxFactor1,dCompY,1);
    lambda.mult(scale);
    for (MFIter mfi(lambda); mfi.isValid(); ++mfi)
    {
        FArrayBox&       lam = lambda[mfi];
        const FArrayBox& R   = Rho[mfi];
        const Box& box = mfi.validbox();
        lam.divide(R,box,sCompR,1,1);
        lam.divide(R,box,sCompR,0,1);
        lam.plus(1.);
        lam.invert(1.);
    }
    lambda.mult(mcdd_relaxFactor);
}

void
HeatTransfer::mcdd_average_state(const MultiFab& SF,
                                 int             sCompY,
                                 int             sCompH,
                                 const MultiFab& rhoF,  // unused for now
                                 int             sCompFR,
                                 MultiFab&       SC,
                                 int             dCompY,
                                 int             dCompH,
                                 const MultiFab& rhoC,
                                 int             sCompCR) const
{
    // Scale Y,H by rho
    MultiFab rhoSF(SF.boxArray(),nspecies+1,0);
    for (MFIter mfi(rhoSF); mfi.isValid(); ++mfi)
    {
        FArrayBox&       RSf = rhoSF[mfi];
        const FArrayBox&  Sf = SF[mfi];
        const Box& box = mfi.validbox();

        RSf.copy(Sf,box,sCompH,box,nspecies,1);
        RSf.copy(Sf,box,sCompY,box,0,nspecies);

        RSf.mult(RSf,box,sCompFR,nspecies,1);
        for (int i=0; i<nspecies; ++i)
            RSf.mult(RSf,box,sCompFR,i,1);
    }
    
    // Do average
    DDOp::average(SC,dCompH,rhoSF,nspecies,1);
    DDOp::average(SC,dCompY,rhoSF,0,nspecies);

    // Undo rho-scaling with coarse rho
    for (MFIter mfi(SC); mfi.isValid(); ++mfi)
    {
        FArrayBox&       Sc = SC[mfi];
        const FArrayBox& Rc = rhoC[mfi];
        const Box& box = mfi.validbox();

        Sc.divide(Rc,box,sCompCR,dCompH,1);
        for (int i=0; i<nspecies; ++i)
            Sc.divide(Rc,box,sCompCR,dCompY+i,1);
    }
}

void
HeatTransfer::mcdd_interp_state(const MultiFab& SC,
                                int             sCompY,
                                int             sCompH,
                                const MultiFab& rhoC,
                                int             sCompCR,
                                MultiFab&       SF,
                                int             dCompY,
                                int             dCompH,
                                const MultiFab& rhoF,
                                int             sCompFR) const
{
    // Scale initial coarse data by rho
    MultiFab rhoSC(SC.boxArray(),nspecies+1,0);
    for (MFIter mfi(rhoSC); mfi.isValid(); ++mfi)
    {
        FArrayBox&       RSc = rhoSC[mfi];
        const FArrayBox&  Sc = SC[mfi];
        const FArrayBox&  Rc = rhoC[mfi];
        const Box& box = mfi.validbox();

        RSc.copy(Sc,box,sCompH,box,nspecies,1);
        RSc.copy(Sc,box,sCompY,box,0,nspecies);

        RSc.mult(Rc,box,sCompCR,nspecies,1);
        for (int i=0; i<nspecies; ++i)
            RSc.mult(Rc,box,sCompCR,i,1);
    }

    // Scale initial fine data by rho
    for (MFIter mfi(SF); mfi.isValid(); ++mfi)
    {
        FArrayBox&       Sf = SF[mfi];
        const FArrayBox& Rf = rhoF[mfi];
        const Box& box = mfi.validbox();

        Sf.mult(Rf,box,sCompFR,dCompH,1);
        for (int i=0; i<nspecies; ++i)
            Sf.mult(Rf,box,sCompFR,dCompY+i,1);
    }

    // Add interpolated/scaled coarse data to scaled fine data
    DDOp::interpolate(SF,dCompH,rhoSC,nspecies,1);
    DDOp::interpolate(SF,dCompY,rhoSC,0,nspecies);

    // Undo rho-scaling of fine sum
    for (MFIter mfi(SF); mfi.isValid(); ++mfi)
    {
        FArrayBox&       Sf = SF[mfi];
        const FArrayBox& Rf = rhoF[mfi];
        const Box& box = mfi.validbox();

        Sf.divide(Rf,box,sCompFR,dCompH,1);
        for (int i=0; i<nspecies; ++i)
            Sf.divide(Rf,box,sCompFR,dCompY+i,1);
    }
}

void
HeatTransfer::mcdd_v_cycle(MultiFab&         S,
                           int               sCompYS,
                           int               sCompHS,
                           const MultiFab&   Rhs,
                           int               sCompYR,
                           int               sCompHR,
                           MultiFab&         T,
                           int               sCompT,
                           const MultiFab&   Rho,
                           int               sCompR,
                           PArray<MultiFab>& flux,
                           int               dCompFY,
                           int               dCompFH,
                           Real              time,
                           Real              dt,
                           int               level)
{
    // S contains Y and H (not rho.Y, rhoH)
    const BoxArray& mg_grids = S.boxArray();

    // Form Res = Rhs - (rho.phi)^np1 - theta*dt*L(phi)^np1
    const int dCompY = 0;
    const int dCompH = nspecies;
    const int nGrow = 0;
    const int nGrowOp = 1;
    MultiFab Res(mg_grids,nspecies+1,nGrow);
    MultiFab lambda(mg_grids,2,nGrow);

    const int lCompY = 0;
    const int lCompH = 1;
    compute_mcdd_relax_parameter(lambda,lCompY,lCompH,S,sCompYS,T,sCompT,
                                 Rho,sCompR,dt,level);

    // Divide lambda by rho
    for (MFIter mfi(lambda); mfi.isValid(); ++mfi)
    {
        FArrayBox&       lam = lambda[mfi];
        const FArrayBox&   R = Rho[mfi];
        const Box& box = mfi.validbox();

        lam.divide(R,box,sCompR,0,2);
    }

    Real err0_Y, err0_H;
    for (int iter=0; iter<mcdd_presmooth; ++iter)
    {
        mcdd_residual(Res,dCompH,Res,dCompY,
                      S,sCompHS,S,sCompYS,
                      T,sCompT,Rho,sCompR,
                      Rhs,sCompHR,Rhs,sCompYR,
                      flux,dCompFH,flux,dCompFY,time,dt,level);

        Real err_Y = MFnorm(Res,0,nspecies);
        Real err_H = MFnorm(Res,nspecies,1);
        if (iter==0)
        {
            err0_Y = err_Y;
            err0_H = err_H;
        }

        if ( std::max(err_Y/err0_Y,err_H/err0_H) < mcdd_rtol)
            iter = mcdd_presmooth;

        for (int i=0; i<level; ++i)
            std::cout << '\t';
        std::cout << "mcdd_v_cycle: " << err_Y << ", " << err_H << '\n';
        
        // Do relaxation by setting S = S + R*lambda (make sure to leave Res in tact for coarse correction below)
        FArrayBox t;
        for (MFIter mfi(Res); mfi.isValid(); ++mfi)
        {
            FArrayBox&         s = S[mfi];
            const FArrayBox& res = Res[mfi];
            const FArrayBox& lam = lambda[mfi];
            const Box& box = mfi.validbox();

            t.resize(box,nspecies+1);
            t.copy(res,box,0,box,0,nspecies+1);
            for (int i=0; i<nspecies; ++i)
                t.mult(lam,box,lCompY,dCompY+i,1);
            t.mult(lam,box,lCompH,dCompH,1);

            s.plus(t,box,dCompH,sCompHS,1);
            s.plus(t,box,dCompY,sCompYS,nspecies);
        }
    }    

    if (MCDDOp.coarser_exists(level))
    {
        const IntVect MGIV(D_DECL(2,2,2));
        const BoxArray c_grids = BoxArray(mg_grids).coarsen(MGIV);
        MultiFab S_save(c_grids,nspecies+1,nGrow);
        MultiFab S_avg(c_grids,nspecies+1,nGrowOp);
        MultiFab Rhs_avg(c_grids,nspecies+1,nGrow);
        MultiFab L_of_S_avg(c_grids,nspecies+1,nGrow);
        MultiFab T_avg(c_grids,1,nGrowOp);
        MultiFab Rho_avg(c_grids,1,nGrow);
        //
        // Build Rhs for the coarse problem:
        //   Avg(res) + L(Avg(S))       [for res, use Res from last iterate of pre_smooth loop above]
        //
        DDOp::average(Rhs_avg,0,Res,0,nspecies+1);
        DDOp::average(Rho_avg,0,Rho,sCompR,1);
        DDOp::average(T_avg,0,T,sCompT,1);

        mcdd_average_state(S,sCompYS,sCompHS,Rho,sCompR,
                           S_avg,dCompY,dCompH,Rho_avg,0);

        PArray<MultiFab> fluxC(BL_SPACEDIM,PArrayManage);
        for (int dir=0; dir<BL_SPACEDIM; ++dir)
            fluxC.set(dir,new MultiFab(BoxArray(c_grids).surroundingNodes(dir),
                                       nspecies+1,0));

        const int c_level = level + 1;
        mcdd_apply(L_of_S_avg,dCompH,L_of_S_avg,dCompY,
                   S_avg,dCompH,S_avg,dCompY,
                   T_avg,0,Rho_avg,0,
                   fluxC,dCompFH,fluxC,dCompFY,time,dt,c_level);

        // Here, Avg(res) += L(Avg(S))
        for (MFIter mfi(Rhs_avg); mfi.isValid(); ++mfi)
        {
            FArrayBox&       avgRes = Rhs_avg[mfi];
            const FArrayBox&  lAvgS = L_of_S_avg[mfi];
            const Box& box = mfi.validbox();
            avgRes.plus(lAvgS,0,0,nspecies+1);
        }

        // Save a copy of pre-relaxed coarse state
        MultiFab::Copy(S_save,S_avg,0,0,nspecies+1,0);
        
        mcdd_v_cycle(S_avg,dCompY,dCompH,Rhs_avg,dCompY,dCompH,
                     T_avg,0,Rho_avg,0,fluxC,dCompY,dCompH,time,dt,c_level);

        // Compute coarse-grid correction, dS = S_avg_post - S_avg_pre
        for (MFIter mfi(S_save); mfi.isValid(); ++mfi)
        {
            FArrayBox&       dS   = S_save[mfi];
            const FArrayBox& newS = S_avg[mfi];

            dS.negate();
            dS.plus(newS,0,0,nspecies+1);
        }
        
        // Increment current solution with interpolated correction
        mcdd_interp_state(S_save,dCompY,dCompH,Rho_avg,0,
                          S,sCompYS,sCompHS,Rho,sCompR);
    }

    // Do post smooth
    for (int iter=0; iter<mcdd_postsmooth; ++iter)
    {
        mcdd_residual(Res,dCompH,Res,dCompY,
                      S,sCompHS,S,sCompYS,
                      T,sCompT,Rho,sCompR,
                      Rhs,sCompHR,Rhs,sCompYR,
                      flux,dCompFH,flux,dCompFY,time,dt,level);

        Real err_Y = MFnorm(Res,0,nspecies);
        Real err_H = MFnorm(Res,nspecies,1);
        if (iter==0)
        {
            err0_Y = err_Y;
            err0_H = err_H;
        }

        if ( std::max(err_Y/err0_Y,err_H/err0_H) < mcdd_rtol)
            iter = mcdd_postsmooth;

        for (int i=0; i<level; ++i)
            std::cout << '\t';
        std::cout << "mcdd_v_cycle: " << err_Y << ", " << err_H << '\n';

        // Do relaxation by setting S = S + R*lambda  (overwrite Res, we don't need it after this step)
        for (MFIter mfi(Res); mfi.isValid(); ++mfi)
        {
            FArrayBox& s   = S[mfi];
            FArrayBox& res = Res[mfi];
            const FArrayBox& lam = lambda[mfi];
            const Box& box = mfi.validbox();

            for (int i=0; i<nspecies; ++i)
                res.mult(lam,box,lCompY,dCompY+i,1);
            res.mult(lam,box,lCompH,dCompH,1);

            s.plus(res,box,dCompH,sCompHS,1);
            s.plus(res,box,dCompY,sCompYS,nspecies);
        }
    }
}

void
HeatTransfer::mcdd_update(Real time,
                          Real dt)
{
    BL_ASSERT(do_mcdd);
    const int nGrow = 0;

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... mcdd update for RhoH and RhoY" << '\n';

    // Get advection updates into new-time state
    scalar_advection_update(dt, first_spec, last_spec);
    scalar_advection_update(dt, RhoH, RhoH);
    if (hack_nospecdiff==1)
        return;

    MultiFab Rhs(grids,nspecies+1,nGrow,Fab_allocate); //stack H on Y's
    const int dCompY = 0;
    const int dCompH = nspecies;

    PArray<MultiFab> fluxn(BL_SPACEDIM,PArrayManage);
    for (int dir=0; dir<BL_SPACEDIM; ++dir)
        fluxn.set(dir,new MultiFab(BoxArray(grids).surroundingNodes(dir),
                                   nspecies+1,0)); //stack H on Y's

    // Build Rhs = rho.phi - (1-theta)*L(phi) - Div(u.rho.phi),
    //   here, L(phi) = Div(rho.Yi.Vi)/Vol, Vi is diffusion velocity
    //   Note that at this point, S_new = rho.phi - Div(u.rho.phi)
    compute_mcdd_visc_terms(Rhs,dCompY,Rhs,dCompH,time,nGrow,DDOp::DD_RhoH,&fluxn,dCompY,dCompH);
    Rhs.mult( -(1.0-be_cn_theta)*dt );

    MultiFab& S_new = get_new_data(State_Type);
    for (MFIter mfi(Rhs); mfi.isValid(); ++mfi)
    {
        FArrayBox& rhs = Rhs[mfi];
        const FArrayBox& snew = S_new[mfi];
        const Box& box = mfi.validbox();

        rhs.plus(snew,box,first_spec,dCompY,nspecies);
        rhs.plus(snew,box,RhoH,dCompH,1);
    }

    PArray<MultiFab> fluxnp1(BL_SPACEDIM,PArrayManage);
    for (int dir=0; dir<BL_SPACEDIM; ++dir)
        fluxnp1.set(dir,new MultiFab(BoxArray(grids).surroundingNodes(dir),
                                     nspecies+1,nGrow)); //stack H on Y's

    // Solve the system...write it out explicitly here
    fill_mcdd_boundary_data(time+dt);
    const int nGrowOp = 1;
    MultiFab S(grids,nspecies+1,nGrowOp);

    // Build Y,H from state
    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        FArrayBox&          YH = S[mfi];
        const FArrayBox& state = S_new[mfi];
        const Box& box = mfi.validbox();

        YH.copy(state,box,RhoH,box,dCompH,1);
        YH.divide(state,box,Density,dCompH,1);

        YH.copy(state,box,first_spec,box,dCompY,nspecies);
        for (int i=0; i<nspecies; ++i)
            YH.divide(state,box,Density,dCompY+i,1);
    }

    mcdd_v_cycle(S,dCompY,dCompH,Rhs,dCompY,dCompH,S_new,Temp,S_new,Density,
                 fluxnp1,dCompY,dCompH,time,dt);

    // Put solution back into state (after scaling by rhonew)
    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        FArrayBox&       state = S_new[mfi];
        const FArrayBox& YH    = S[mfi];
        const Box& box = mfi.validbox();

        state.copy(YH,box,dCompH,box,RhoH,1);
        state.mult(state,box,Density,RhoH,1);

        state.copy(YH,box,dCompY,box,first_spec,nspecies);
        for (int i=0; i<nspecies; ++i)
            state.mult(state,box,Density,first_spec+i,1);
    }

    if (do_reflux)
    {
        // Build the total flux (pieces from n and np1) for RY and RH
        FArrayBox fluxtot, fluxtmp;
        for (int d = 0; d < BL_SPACEDIM; d++)
        {
            for (MFIter mfi(fluxn[d]); mfi.isValid(); ++mfi)
            {
                const FArrayBox& fn = fluxn[d][mfi];
                const FArrayBox& fnp1 = fluxnp1[d][mfi];
                const Box& ebox = fn.box();

                fluxtot.resize(ebox,nspecies+1);
                fluxtmp.resize(ebox,nspecies+1);
                fluxtot.copy(fn,ebox,0,ebox,0,nspecies+1);
                fluxtot.mult(1.0-be_cn_theta);
                fluxtmp.copy(fnp1,ebox,0,ebox,0,nspecies+1);
                fluxtmp.mult(be_cn_theta);
                fluxtot.plus(fluxtmp,ebox,0,0,nspecies+1);
                if (level < parent->finestLevel())
                {
                    // Note: The following inits do not trash eachother
                    //  since the component ranges don't overlap...
                    getLevel(level+1).getViscFluxReg().CrseInit(fluxtot,ebox,
                                                                d,dCompY,first_spec,
                                                                nspecies,-dt);
                    getLevel(level+1).getViscFluxReg().CrseInit(fluxtot,ebox,
                                                                d,dCompH,RhoH,
                                                                1,-dt);
                }
                if (level > 0)
                {
                    getViscFluxReg().FineAdd(fluxtot,d,mfi.index(),
                                             dCompY,first_spec,nspecies,dt);
                    getViscFluxReg().FineAdd(fluxtot,d,mfi.index(),
                                             dCompH,RhoH,1,dt);
                }
            }
        }
        if (level < parent->finestLevel())
            getLevel(level+1).getViscFluxReg().CrseInitFinish();
    }
}

void
HeatTransfer::mcdd_apply(MultiFab& AH, int dCompH, MultiFab& AY, int dCompY,
                         const MultiFab& H, int sCompH, const MultiFab& Y, int sCompY,
                         MultiFab& T, int sCompT, const MultiFab& Rho, int sCompR,
                         PArray<MultiFab>& fluxH, int dCompFH,
                         PArray<MultiFab>& fluxY, int dCompFY,
                         Real time, Real dt, int level)
{
    // Make sure T is consistent with Y,H
    int  max_iters_taken = 0;
    bool error_occurred  = false;
    for (MFIter mfi(T); mfi.isValid(); ++mfi)
    {
        FArrayBox&       Tfab = T[mfi];
        const FArrayBox& Hfab = H[mfi];
        const FArrayBox& Yfab = Y[mfi];
        const Box& box = mfi.validbox();
        
        int iters_taken = getChemSolve().getTGivenHY(Tfab,Hfab,Yfab,
                                                     box,sCompH,sCompY,sCompT);
        
        error_occurred = error_occurred  ||  iters_taken < 0;
        max_iters_taken = std::max(max_iters_taken, iters_taken);        
    }
    
    ParallelDescriptor::ReduceBoolOr(error_occurred);
    ParallelDescriptor::ReduceIntMax(max_iters_taken);
    
    if (verbose && ParallelDescriptor::IOProcessor())
    {
        if (error_occurred)
            BoxLib::Error("HeatTransfer::RhoH_to_Temp: error in H->T");
        
        for (int i=0; i<level; ++i)
            std::cout << '\t';
        std::cout << "     mcdd_residual::Rho_to_Temp: max iterations = "
             << max_iters_taken << '\n';
    }

    MCDDOp.applyOp(AH,dCompH,AY,dCompY,T,sCompT,Y,sCompY,
                   fluxH,dCompFH,fluxY,dCompFY,DDOp::DD_RhoH,level);

    add_heat_sources(AH,dCompH,time,0,1.0);

    AH.mult(-be_cn_theta*dt,dCompH,1);
    AY.mult(-be_cn_theta*dt,dCompY,nspecies);
}

void
HeatTransfer::mcdd_residual(MultiFab& ResH, int dCompH, MultiFab& ResY, int dCompY,
                            const MultiFab& H, int sCompH, const MultiFab& Y, int sCompY,
                            MultiFab& T, int sCompT, const MultiFab& Rho, int sCompR,
                            const MultiFab& RhsH, int sCompRH,
                            const MultiFab& RhsY, int sCompRY,
                            PArray<MultiFab>& fluxH, int dCompFH,
                            PArray<MultiFab>& fluxY, int dCompFY,
                            Real time, Real dt, int level)
{
    mcdd_apply(ResH,dCompH,ResY,dCompY,H,sCompH,Y,sCompY,T,sCompT,Rho,sCompR,
               fluxH,dCompFH,fluxY,dCompFY,time,dt,level);

    FArrayBox rhoPhi;
    for (MFIter mfi(ResH); mfi.isValid(); ++mfi)
    {
        FArrayBox&       resH = ResH[mfi];
        FArrayBox&       resY = ResY[mfi];
        const FArrayBox& Hfab = H[mfi];
        const FArrayBox& Yfab = Y[mfi];
        const FArrayBox& rhsH = RhsH[mfi];
        const FArrayBox& rhsY = RhsY[mfi];
        const FArrayBox& rho = Rho[mfi];
        const Box& box = mfi.validbox();

        rhoPhi.resize(box,1);
        rhoPhi.copy(Hfab,box,sCompH,box,0,1);
        rhoPhi.mult(rho,box,sCompR,0,1);
        resH.minus(rhoPhi,box,0,dCompH,1);

        for (int i=0; i<nspecies; ++i)
        {
            rhoPhi.copy(Yfab,box,sCompY+i,box,0,1);
            rhoPhi.mult(rho,box,sCompR,0,1);
            resY.minus(rhoPhi,box,0,dCompY+i,1);
        }

        resH.plus(rhsH,box,sCompRH,dCompH,1);
        resY.plus(rhsY,box,sCompRY,dCompY,nspecies);
    }
}

void
HeatTransfer::rk_diffusion_operator (const Real time,
				     const Real scale_factor,
				     MultiFab *& div_of_flux_for_H,
				     MultiFab *& div_of_flux_for_Y,
				     MultiFab **& flux_for_H,
				     MultiFab **& flux_for_Y)
{
/*
  evaluate the "operator" (extensive fluxes and their divergences) for the 
  Runge-Kutta implementation of the diffusion update using the mixture
  averaged or multicomponent formulation.
  
  the divergences are scaled by 1/vol so they do approximate the analytic
  divergence.  when used in the Runge-Kutta formula their additional scaling,
  given by scale_factor, should be set to - dt.  the minus occurs because
  the standard ODE is written y_prime = f(y), that is, the divergences are
  moved to the opposite side of the equation from the time derivative.  when
  used in getViscTerms the scale_factor should be set to -1.

  Real time                       ! INPUT time, either prev_time or cur_time
  Real scale_factor               ! INPUT scale factor
  MultiFab *& div_of_flux_for_H   ! OUTPUT divergence of the flux for rho H weighted by scale_factor / vol
  MultiFab *& div_of_flux_for_Y   ! OUTPUT divergence of the flux for rho Y weighted by scale_factor / vol
  MultiFab **& flux_for_H         ! OUTPUT extensive rho H flux
  MultiFab **& flux_for_Y         ! OUTPUT extensive rho Y flux
*/

    // check that time is either prev_time or cur_time
    const TimeLevel whichTime = which_time(State_Type, time);
    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    // get some constants used as dimensions
    int ncomps = NUM_STATE;
    // const int nspecies = getChemSolve().numSpecies(); // this is globally defined

    // get some constants used in the calculation of temperature
    Real maximum_error = getChemSolve().getHtoTerrMAX ();
    int maximum_iterations = getChemSolve().getHtoTiterMAX ();

    // get locations of things in the state and convert to fortran indexing
    int index_of_firstY = Density + 1;
    int index_of_lastY = index_of_firstY + nspecies - 1;
    int index_of_rho = Density;
    int index_of_rhoH = RhoH;
    int index_of_T = Temp;
    int fort_index_of_firstY = 1 + index_of_firstY;
    int fort_index_of_lastY  = 1 + index_of_lastY;
    int fort_index_of_rho    = 1 + index_of_rho;
    int fort_index_of_rhoH   = 1 + index_of_rhoH;
    int fort_index_of_T      = 1 + index_of_T;

    MultiFab area[BL_SPACEDIM], volume;

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        geom.GetFaceArea(area[dir],grids,dir,GEOM_GROW);
    }
    geom.GetVolume(volume,grids,GEOM_GROW);

    // allocate OUTPUT multifabs for fluxes and updates
    int ngrow = 0;
    diffusion->allocFluxBoxesLevel(flux_for_H,ngrow,1);
    diffusion->allocFluxBoxesLevel(flux_for_Y,ngrow,nspecies);
    div_of_flux_for_H = new MultiFab(grids,1,ngrow);
    div_of_flux_for_Y = new MultiFab(grids,nspecies,ngrow);

    // loop over fabs in the state at the specified time
    MultiFab dummy (grids,1,0,Fab_noallocate);
    MFIter div_of_flux_for_H_mfi(*div_of_flux_for_H);
    MFIter div_of_flux_for_Y_mfi(*div_of_flux_for_Y);
    MFIter xflux_for_H_mfi(*flux_for_H[0]);
    MFIter yflux_for_H_mfi(*flux_for_H[1]);
    MFIter xflux_for_Y_mfi(*flux_for_Y[0]);
    MFIter yflux_for_Y_mfi(*flux_for_Y[1]);
    ngrow = 1;
    for (FillPatchIterator state_fpi (*this, dummy, ngrow, time, State_Type, 0, ncomps);
         state_fpi.isValid();
         ++state_fpi, 
	     ++div_of_flux_for_H_mfi, 
	     ++div_of_flux_for_Y_mfi,
	     ++xflux_for_H_mfi,
	     ++yflux_for_H_mfi,
	     ++xflux_for_Y_mfi,
	     ++yflux_for_Y_mfi)
    {
        BL_ASSERT (
	    div_of_flux_for_H_mfi.isValid() &&
	    div_of_flux_for_Y_mfi.isValid() &&
	    xflux_for_H_mfi.isValid() &&
	    yflux_for_H_mfi.isValid() &&
	    xflux_for_Y_mfi.isValid() &&
	    yflux_for_Y_mfi.isValid()
	    );

	// get index of the present box
        const int idx = state_fpi.index();

	// get boundary condition array for all components
	Array<int> bc = getBCArray (State_Type, idx, 0, ncomps);

/*
c     arguments are alphabetical, mostly:
c
c     domain_lo, domain_hi,             ! INPUT limits of valid region of the domain
c     lo, hi,                           ! INPUT limits of valid region of the box
c   * areax, DIMS(areax),               ! INPUT areas of the faces perendicular to x axis
c   * areay, DIMS(areay),               ! INPUT areas of the faces perpendicular to y axis
c     bc,                               ! INPUT boundary condition array for all comps
c     dx,                               ! INPUT physical dimensions of grid cells
c     index_of_firstY,                  ! INPUT index of rho Y for the first species in the state
c     index_of_lastY,                   ! INPUT index of rho Y for the last species in the state
c     index_of_rho,                     ! INPUT index of rho in the state
c     index_of_rhoH,                    ! INPUT index of rho H in the state
c     index_of_T,                       ! INPUT index of T in the state
c     maximum_error,                    ! INPUT maximum error in calculation of T
c     maximum_iterations,               ! INPUT maximum iterations in calculation of T
c     ncomps,                           ! INPUT total number of components in the state
c     nspecies,                         ! INPUT total number of species in the state
c     scale_factor,                     ! INPUT scale_factor
c     state, DIMS(state),               ! INPUT all variables in the state
c   * volume, DIMS(volume),             ! INPUT volumes of the cells
c     div_of_flux_for_H, DIMS(div_of_flux_for_H), ! OUTPUT divergence of the flux for rho H
c     div_of_flux_for_Y, DIMS(div_of_flux_for_Y), ! OUTPUT divergences of the fluxes for rho Y
c     xflux_for_H, DIMS(xflux_for_H),   ! OUTPUT extensive x fluxes for rho H
c     xflux_for_Y, DIMS(xflux_for_Y),   ! OUTPUT extensive x fluxes for rho Y
c     yflux_for_H, DIMS(yflux_for_H),   ! OUTPUT extensive y fluxes for rho H
c     yflux_for_Y, DIMS(yflux_for_Y),   ! OUTPUT extensive y fluxes for rho Y
c
c     * these arguments are not used
*/

#if (BL_SPACEDIM==2) 
#define DATA_AND_LIMITS(foo) foo.dataPtr(),foo.loVect()[0],foo.loVect()[1],foo.hiVect()[0],foo.hiVect()[1]
#endif
#if (BL_SPACEDIM==3) 
#define DATA_AND_LIMITS(foo) foo.dataPtr(),foo.loVect()[0],foo.loVect()[1],foo.loVect()[2],foo.hiVect()[0],foo.hiVect()[1],foo.hiVect()[2]
#endif

	if (!rk_mixture_averaged)
	{
	    // multicomponent is the default
	    FORT_RK_MULTICOMPONENT
		(geom.Domain().loVect(), geom.Domain().hiVect(), 
		 grids[idx].loVect(), grids[idx].hiVect(),
		 DATA_AND_LIMITS(area[0][idx]),
		 DATA_AND_LIMITS(area[1][idx]),
		 bc.dataPtr(),
		 geom.CellSize(),
		 &fort_index_of_firstY,
		 &fort_index_of_lastY,
		 &fort_index_of_rho,
		 &fort_index_of_rhoH,
		 &fort_index_of_T,
		 &maximum_error,
		 &maximum_iterations,
		 &ncomps,
		 &nspecies,
		 &scale_factor,
		 DATA_AND_LIMITS(state_fpi()),
		 DATA_AND_LIMITS(volume[idx]),
		 DATA_AND_LIMITS((*div_of_flux_for_H)[div_of_flux_for_H_mfi]),
		 DATA_AND_LIMITS((*div_of_flux_for_Y)[div_of_flux_for_Y_mfi]),
		 DATA_AND_LIMITS((*flux_for_H[0])[xflux_for_H_mfi]),
		 DATA_AND_LIMITS((*flux_for_Y[0])[xflux_for_Y_mfi]),
		 DATA_AND_LIMITS((*flux_for_H[1])[yflux_for_H_mfi]),
		 DATA_AND_LIMITS((*flux_for_Y[1])[yflux_for_Y_mfi]));
	}
	else
	{
	    // do mixture averaged
	    FORT_RK_MIXTURE_AVERAGED
		(geom.Domain().loVect(), geom.Domain().hiVect(), 
		 grids[idx].loVect(), grids[idx].hiVect(),
		 DATA_AND_LIMITS(area[0][idx]),
		 DATA_AND_LIMITS(area[1][idx]),
		 bc.dataPtr(),
		 geom.CellSize(),
		 &fort_index_of_firstY,
		 &fort_index_of_lastY,
		 &fort_index_of_rho,
		 &fort_index_of_rhoH,
		 &fort_index_of_T,
		 &maximum_error,
		 &maximum_iterations,
		 &ncomps,
		 &nspecies,
		 &scale_factor,
		 DATA_AND_LIMITS(state_fpi()),
		 DATA_AND_LIMITS(volume[idx]),
		 DATA_AND_LIMITS((*div_of_flux_for_H)[div_of_flux_for_H_mfi]),
		 DATA_AND_LIMITS((*div_of_flux_for_Y)[div_of_flux_for_Y_mfi]),
		 DATA_AND_LIMITS((*flux_for_H[0])[xflux_for_H_mfi]),
		 DATA_AND_LIMITS((*flux_for_Y[0])[xflux_for_Y_mfi]),
		 DATA_AND_LIMITS((*flux_for_H[1])[yflux_for_H_mfi]),
		 DATA_AND_LIMITS((*flux_for_Y[1])[yflux_for_Y_mfi]));
	}
    }
}

Real
HeatTransfer::rk_step_selection (const Real time)
{
/*
  estimate the time step for the Runge-Kutta implementation of the diffusion
  update.  this estimation is based on the diffusion coefficients of the 
  species into the mixture, independent of whether or not the "operator" is 
  based on the mixture averaged or the multicomponent formulation with Soret 
  and Dufour effects.

                                space_step ** 2
                  time_step  =  ---------------
                                D_mix * 2 * DIM

  the minimum of this formula is taken over all cells and species

  Real time     ! INPUT time, either prev_time or cur_time
  returned Real ! OUTPUT the time step
*/

    // check that time is either prev_time or cur_time
    const TimeLevel whichTime = which_time(State_Type, time);
    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    // get some constants used as dimensions
    int ncomps = NUM_STATE;
    // const int nspecies = getChemSolve().numSpecies(); // this is globally defined

    // get some constants used in the calculation of temperature
    Real maximum_error = getChemSolve().getHtoTerrMAX ();
    int maximum_iterations = getChemSolve().getHtoTiterMAX ();

    // get locations of things in the state and convert to fortran indexing
    int index_of_firstY = Density + 1;
    int index_of_lastY = index_of_firstY + nspecies - 1;
    int index_of_rho = Density;
    int index_of_rhoH = RhoH;
    int index_of_T = Temp;
    int fort_index_of_firstY = 1 + index_of_firstY;
    int fort_index_of_lastY  = 1 + index_of_lastY;
    int fort_index_of_rho    = 1 + index_of_rho;
    int fort_index_of_rhoH   = 1 + index_of_rhoH;
    int fort_index_of_T      = 1 + index_of_T;

    // get the state at the desired time
    MultiFab& state 
	= (whichTime == AmrOldTime) 
	? get_old_data(State_Type) 
	: get_new_data(State_Type);

    // since it is possible that a processor has no boxes on this
    // level, the variable that holds the smallest time step found
    // here must be initialized with a large value so that the 
    // ParallelDescriptor::ReduceRealMin will function propoerly.
    Real smallest_time_step = 1.0e20;

    // loop over fabs in the state
    for (MFIter state_mfi(state); state_mfi.isValid(); ++state_mfi)
    {
	// get index of the present box
        const int idx = state_mfi.index();

/*
c     arguments are alphabetical, mostly:
c
c     lo, hi             ! INPUT limits of valid region of the box
c     dx                 ! INPUT physical dimensions of grid cells
c     index_of_firstY    ! INPUT index of rho Y for the first species in the state
c     index_of_lastY     ! INPUT index of rho Y for the last species in the state
c     index_of_rho       ! INPUT index of rho in the state
c     index_of_rhoH      ! INPUT index of rho H in the state
c     index_of_T         ! INPUT index of T in the state
c     maximum_error      ! INPUT maximum error in calculation of T
c     maximum_iterations ! INPUT maximum iterations in calculation of T
c     ncomps             ! INPUT total number of components in the state
c     nspecies           ! INPUT total number of species in the state
c     smallest_time_step ! OUTPUT smallest time step over all cells and species
c     state, DIMS(state) ! INPUT all variables in the state
*/

#if (BL_SPACEDIM==2) 
#define DATA_AND_LIMITS(foo) foo.dataPtr(),foo.loVect()[0],foo.loVect()[1],foo.hiVect()[0],foo.hiVect()[1]
#endif
#if (BL_SPACEDIM==3) 
#define DATA_AND_LIMITS(foo) foo.dataPtr(),foo.loVect()[0],foo.loVect()[1],foo.loVect()[2],foo.hiVect()[0],foo.hiVect()[1],foo.hiVect()[2]
#endif

	FORT_RK_STEP_SELECTION
	    (grids[idx].loVect(), grids[idx].hiVect(),
	     geom.CellSize(),
	     &fort_index_of_firstY,
	     &fort_index_of_lastY,
	     &fort_index_of_rho,
	     &fort_index_of_rhoH,
	     &fort_index_of_T,
	     &maximum_error,
	     &maximum_iterations,
	     &ncomps,
	     &nspecies,
	     &smallest_time_step,
	     DATA_AND_LIMITS(state[state_mfi])
	     );
    }
    // reconcile the time step over all the processors
    ParallelDescriptor::ReduceRealMin (smallest_time_step);

    // apply the safety factor
    Real time_step = rk_time_step_multiplier * smallest_time_step;

    // return the scaled time step
    return time_step;
}

void
HeatTransfer::compute_differential_diffusion_terms (MultiFab& visc_terms,
						    int       sComp,
                                                    Real      time)
{
    if (hack_nospecdiff)
    {
        if (verbose && ParallelDescriptor::IOProcessor())
            std::cout << "... HACK!!! zeroing spec diffusion terms " << std::endl;
        visc_terms.setVal(0.0,sComp,nspecies);
        return;            
    }
    //
    // NOTE: This routine does not fill grow cells
    //
    BL_ASSERT(visc_terms.boxArray() == grids);
    BL_ASSERT(visc_terms.nComp() >= sComp+nspecies);

    MultiFab& S = get_data(State_Type,time);
    const Real* dx = geom.CellSize();

    const Array<int> rho_flag(nspecies,2); // Hardwired, for now.
    const int nGrowOp = 1; // Required by the operator to compute first-cut fluxes
    MultiFab s_tmp(grids,1,nGrowOp);
    //
    // FIXME: Should have this in level data
    //
    MultiFab **beta;
    diffusion->allocFluxBoxesLevel(beta,0,nspecies);
    getDiffusivity(beta, time, first_spec, 0, nspecies);

    MultiFab **flux;
    diffusion->allocFluxBoxesLevel(flux,0,1);

    const TimeLevel whichTime = which_time(State_Type,time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);
    BL_ASSERT(first_spec == Density+1);
    //
    // Create and fill a full MultiFab of all species at this level and below.
    //
    MultiFab rho_and_species(grids,nspecies+1,nGrowOp);

    for (FillPatchIterator fpi(*this,rho_and_species,nGrowOp,time,State_Type,Density,nspecies+1);
         fpi.isValid();
         ++fpi)
    {
        FArrayBox& fab = rho_and_species[fpi];

        fab.copy(fpi(),0,0,nspecies+1);

        FArrayBox tmp(fab.box(),1);
        tmp.copy(fab,0,0,1);
        tmp.invert(1);

        for (int comp = 0; comp < nspecies; ++comp) 
            if (rho_flag[comp] == 2)
                fab.mult(tmp,0,comp+1,1);
    }

    MultiFab rho_and_species_crse;

    if (level > 0) 
    {
        int nGrow = 1;
        HeatTransfer& coarser  = *(HeatTransfer*) &(parent->getLevel(level-1));

        rho_and_species_crse.define(coarser.grids,nspecies+1,nGrowOp,Fab_allocate);

        for (FillPatchIterator fpi(coarser,rho_and_species_crse,nGrow,time,State_Type,Density,nspecies+1);
             fpi.isValid();
             ++fpi)
        {
            FArrayBox& fab = rho_and_species_crse[fpi];

            fab.copy(fpi(),0,0,nspecies+1);

            FArrayBox tmp(fab.box(),1);
            tmp.copy(fab,0,0,1);
            tmp.invert(1);

            for (int comp = 0; comp < nspecies; ++comp) 
                if (rho_flag[comp] == 2)
                    fab.mult(tmp,0,comp+1,1);
        }
    }

    for (int comp = 0; comp < nspecies; ++comp)
    {
        const int state_ind = first_spec + comp;
	ViscBndry visc_bndry;
	diffusion->getBndryDataGivenS(visc_bndry,rho_and_species,rho_and_species_crse,
                                      state_ind,comp+1,1,time,rho_flag[comp]);

	ABecLaplacian visc_op(visc_bndry,dx);
	visc_op.setScalars(0.0,1.0);
	
	for (int d = 0; d < BL_SPACEDIM; d++)
	{
            MultiFab bcoeffs;
            geom.GetFaceArea(bcoeffs,grids,d,0);
	    for (MFIter mfi(bcoeffs); mfi.isValid(); ++mfi)
		bcoeffs[mfi].mult((*beta[d])[mfi.index()],comp,0,1);
	    visc_op.bCoefficients(bcoeffs,d);
	}

	// JFG: note the arguments for MultiFab:: procedures are 
	// (dst, src, srccomp, dstcomp, ncomp, ngrow);
	MultiFab::Copy(s_tmp,rho_and_species,comp+1,0,1,0);
	
        visc_op.compFlux(D_DECL(*flux[0],*flux[1],*flux[2]),s_tmp);
        MultiFab* const* fluxKeep = (whichTime == AmrOldTime) ? SpecDiffusionFluxn : SpecDiffusionFluxnp1;
	for (int d = 0; d < BL_SPACEDIM; ++d)
	    MultiFab::Copy(*fluxKeep[d],*flux[d],0,comp,1,0);
	spec_diffusion_flux_computed[comp] = HT_ExplicitDiffusion;
    }

    s_tmp.clear();
    rho_and_species.clear();
    rho_and_species_crse.clear();
    diffusion->removeFluxBoxesLevel(flux);
    //
    // Modify update/fluxes to preserve flux sum = 0, compute -Div(flux)
    // (use dt = 1.0,  since the routine actually updates does "state"-=Div(flux)*dt)
    //
    const Real      dt        = 1.0;
    const MultiFab* old_state = 0;
    const MultiFab* delta_rhs = 0;
    const MultiFab* alpha     = 0;
    const int       dataComp  = 0;
    adjust_spec_diffusion_update(visc_terms,old_state,sComp,dt,time,rho_flag,
				 get_rho_half_time(),dataComp,delta_rhs,alpha,beta);
    diffusion->removeFluxBoxesLevel(beta);
}

void
HeatTransfer::getTempViscTerms (MultiFab& visc_terms,
                                int       src_comp, 
                                Real      time)
{
    //
    // If only one species,
    // this computes 1/c_p (div lambda grad T + div q_rad)
    // if more than one species,
    //               1/c_p (div lambda grad T + div q_rad + 
    //               sum_l rho D grad Y_l dot grad h_l)
    //
    // NOTE: This routine does not fill grow cells
    //
    BL_ASSERT(visc_terms.boxArray()==grids);

    const int nGrow     = 0;
    const int rho_flag  = 1;

    MultiFab** beta;
    diffusion->allocFluxBoxesLevel(beta);
    //
    // + div lambda grad T + Q
    //
    getDiffusivity(beta, time, Temp, 0, 1);

    //    VisMF::Write((*beta[0]),"beta_orig");
    //VisMF::Write((*beta[1]),"betay_orig");

    diffusion->getViscTerms(visc_terms,src_comp,Temp,time,rho_flag,0,beta);
    diffusion->removeFluxBoxesLevel(beta);

    //VisMF::Write(visc_terms,"diff_vt_orig");

    add_heat_sources(visc_terms,Temp-src_comp,time,nGrow,1.0);

    //VisMF::Write(visc_terms,"heat_src_orig");

    MultiFab delta_visc_terms(grids,1,nGrow);
    //
    // + sum_l rho D grad Y_l dot grad h_l
    //
    compute_rhoDgradYgradH(time,delta_visc_terms);

    //VisMF::Write(delta_visc_terms,"delta_vt_orig");

    //
    // Add to visc terms, then divide whole mess by c_p 
    //
    FArrayBox cp, spec;

    MultiFab& S = get_data(State_Type,time);

    for (MFIter dvt_mfi(delta_visc_terms); dvt_mfi.isValid(); ++dvt_mfi)
    {
        const Box& box = dvt_mfi.validbox();

        cp.resize(box,1);
        spec.resize(box,nspecies);

        spec.copy(S[dvt_mfi],box,first_spec,box,0,nspecies);

        {
            FArrayBox tmp(box,1);
            tmp.copy(S[dvt_mfi],Density,0,1);
            tmp.invert(1);

            for (int i = 0; i < nspecies; ++i)
                spec.mult(tmp,0,i,1);
        }

        const int yComp   = 0;
        const int sCompCp = 0;
        getChemSolve().getCpmixGivenTY(cp,S[dvt_mfi],spec,box,
                                       Temp,yComp,sCompCp);
        
        visc_terms[dvt_mfi].plus(delta_visc_terms[dvt_mfi],box,0,Temp-src_comp,1);
        visc_terms[dvt_mfi].divide(cp,0,Temp-src_comp,1);
    }
}

void
HeatTransfer::getRhoHViscTerms (MultiFab& visc_terms,
                                int       src_comp, 
                                Real      time)
{
    //
    // Compute the enthalpy source term, including radiative and
    // conductive fluxe divergences.  The remaining term, which
    // is the divergence of the enthalpy flux carried by species
    // diffusion (ie, div(h_i gamma_i), is computed as an advective
    // forcing contribution (because it was more convenient to
    // do it there at the time).  It is not added here, so if the
    // algorithm changes to require the full RhoH forcing term
    // (for example, if RhoH is extrapolated to cell edges), the
    // one needs to be sure to add that missing term.
    //
    // NOTE: This routine does not fill grow cells
    //
    BL_ASSERT(visc_terms.boxArray()==grids);

    MultiFab** beta;
    const int nGrow = 0;
    diffusion->allocFluxBoxesLevel(beta);
    
    const int rhoh_rho_flag = 2;
    getDiffusivity(beta, time, RhoH, 0, 1);

    diffusion->getViscTerms(visc_terms,src_comp,RhoH,time,rhoh_rho_flag,0,beta);
    diffusion->removeFluxBoxesLevel(beta);
    add_heat_sources(visc_terms,RhoH-src_comp,time,nGrow,1.0);
 
}

void
HeatTransfer::add_heat_sources(MultiFab& sum,
                               int       dComp,
                               Real      time,
                               int       nGrow,
                               Real      scale)
{
    //
    // - div q rad
    //
    if (do_OT_radiation || do_heat_sink)
    {
        BL_ASSERT(sum.boxArray() == grids);
        MultiFab dqrad(grids,1,nGrow);
        compute_OT_radloss(time,nGrow,dqrad);
        for (MFIter mfi(sum); mfi.isValid(); ++mfi)
        {
            FArrayBox& sumFab = sum[mfi];
            FArrayBox& Qfab = dqrad[mfi];
            if (scale != 1)
                Qfab.mult(scale);
            sumFab.minus(Qfab,mfi.validbox(),0,dComp,1);
        }
    }    
}

void
HeatTransfer::set_rho_to_species_sum (MultiFab& S,
                                      int       strtcomp, 
                                      int       nghost_in,
                                      int       minzero)
{
    set_rho_to_species_sum(S, strtcomp, S, strtcomp, nghost_in, minzero);
}

//
// This function
//       sets the density component in S_out to the sum of 
//       the species components in S_in
// if minzero = 1, the species components are first "max-ed" w/ zero
//  thus changing S_in values    
//
// s_in_strt is the state component corresponding to the
// 0-th component of S_in. It is otherwise assumed that 
// that the components of S_in "align" with those in S_out.
//
void
HeatTransfer::set_rho_to_species_sum (MultiFab& S_in, 
                                      int       s_in_strt,
                                      MultiFab& S_out,
                                      int       s_out_strt,  
                                      int       nghost_in,
                                      int       minzero)

{
    const BoxArray& sgrids = S_in.boxArray();

    BL_ASSERT(sgrids == S_out.boxArray());

    const int ngrids       = sgrids.size();
    const int s_first_spec = first_spec - s_in_strt;
    const int s_last_spec  = last_spec  - s_in_strt;
    const int s_num_spec   = last_spec - first_spec + 1;
    const int s_density    = Density-s_out_strt;
    const int nghost       = std::min(S_in.nGrow(), std::min(S_out.nGrow(), nghost_in));

    if (minzero)
    {
        for (MFIter mfi(S_in); mfi.isValid(); ++mfi)
        {
            const int i   = mfi.index();
            Box       box = BoxLib::grow(sgrids[i], nghost);
            FabMinMax(S_in[i], box, 0.0, Real_MAX, s_first_spec, s_num_spec);
        }
    }

    BL_ASSERT(s_density >= 0);

    S_out.setVal(0, s_density, 1, nghost);

    for (MFIter mfi(S_in); mfi.isValid(); ++mfi)
    {
        const int i   = mfi.index();
        Box       box = BoxLib::grow(sgrids[i],nghost);

        for (int spec = s_first_spec; spec <= s_last_spec; spec++)
        {
            S_out[i].plus(S_in[i],box,spec,s_density,1);
        }
    }

    make_rho_curr_time();
}

void
HeatTransfer::scale_species (MultiFab& S,
                             int       strtcomp, 
                             int       minzero)
    //
    // This function 
    //       scales the species components in S so
    //       that they add up to the density component.
    //
    // if minzero = 1, the species components are first "max-ed" w/ zero
    //
    // strtcomp is the state component corresponding to the
    // 0-th component of S. It is otherwise assumed that 
    // that the components of S "align" with those in the state.
    //
{
    const BoxArray& sgrids = S.boxArray();
    const int s_density    = Density-strtcomp;

    BL_ASSERT(s_density >= 0);

    const int s_first_spec = first_spec - strtcomp;
    const int s_last_spec  = last_spec  - strtcomp;
    const int nghost       = S.nGrow();

    if (minzero)
    {
        //
        // max (rho*Y)_l w/ zero
        //
        for (MFIter mfi(S); mfi.isValid(); ++mfi)
        {
            const int k   = mfi.index();
            Box       grd = BoxLib::grow(sgrids[k],nghost);
            const int vol = grd.volume();

            for (int spec = s_first_spec; spec <= s_last_spec; spec++)
            {
                if (S[k].min(spec) < 0.0)
                {
                    Real* sdat = S[k].dataPtr(spec);

                    for (int i = 0; i < vol; i++) 
                        sdat[i] = std::max(0.0,sdat[i]);
                }
            }
        }
    }

    MultiFab factor(sgrids,1,nghost);
    //
    // compute sum_l (rho*Y)_l
    //
    factor.setVal(0,nghost);

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        const int k = mfi.index();

        for (int spec = s_first_spec; spec <= s_last_spec; spec++)
        {
            factor[k].plus(S[k],spec,0,1);
        }
    }
    //
    // Compute rho/sum_l (rho*Y)_l and mulitply (rho*Y)_l by this factor
    //
    // We go through the various min/max contortions because S
    // may represent a change to the state instead of the state itself
    //
    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        const int k      = mfi.index();
        Real  min_factor = factor[k].min();
        Real  max_factor = factor[k].max();
        Real  min_rho    = S[k].min(s_density);
        Real  max_rho    = S[k].max(s_density);

        if ((min_factor>0.0||max_factor<0.0) && (min_rho>0.0||max_rho<0.0))
        {
            //
            // Anything potentially worrisome is non-zero --> easy case.
            //
            factor[k].invert(1.0,0,1);
            factor[k].mult(S[k],s_density,0,1);
            for (int spec = s_first_spec; spec <= s_last_spec; spec++)
                S[k].mult(factor[k],0,spec,1);
        }
        else
        {
            //
            //  We are here only if S represents change to the
            //  state, not the state itself
            //
            //  rho has a zero value anr/or sum has a zero value
            //
            //  guiding principal: rho is correct and the species are wrong
            //
            Real* sumdat = factor[k].dataPtr();
            Real* rhodat = S[k].dataPtr(s_density);
            Real rhoi, sumi, factori;

            Box grd = BoxLib::grow(sgrids[k],nghost);
            int vol = grd.volume();

            for (int i = 0; i < vol; i++)
            { 
                sumi = sumdat[i];
                rhoi = rhodat[i];

                if (sumi != 0.0 && rhoi != 0.0)
                {
                    //
                    // Case 1: both non-zero --> do what is done above.
                    //
                    factori = rhoi/sumi;
                    for (int spec = s_first_spec; spec <= s_last_spec; spec++) 
                        S[k].dataPtr(spec)[i] *= factori;
                }
                else if (sumi == 0.0 && rhoi == 0.0)
                {
                    //
                    // Case 2: both zero --> do nothing
                    //
                }
                else
                {
                    //
                    // Cases 3 and 4: one is zero, but not both
                    // --> shift non-zero species so that sum is zero
                    //
                    int nonzero=0;
                    for (int spec = s_first_spec; spec <= s_last_spec; spec++) 
                        if (S[k].dataPtr(spec)[i] != 0.0)
                            nonzero++;
                    Real shift = (rhoi-sumi)/nonzero;
                    for (int spec = s_first_spec; spec <= s_last_spec; spec++) 
                        if (S[k].dataPtr(spec)[i] != 0.0) 
                            S[k].dataPtr(spec)[i] += shift; 
                }
            }
        }
    }
}

void
HeatTransfer::temperature_stats (MultiFab& S)
{
    if (verbose)
    {
        //
        // Calculate some minimums and maximums.
        //
        Real tdhmin[3] = { 1.0e30, 1.0e30, 1.0e30};
        Real tdhmax[3] = {-1.0e30,-1.0e30,-1.0e30};

        for (MFIter S_mfi(S); S_mfi.isValid(); ++S_mfi)
        {
            const Box& bx = S_mfi.validbox();

            tdhmin[0] = std::min(tdhmin[0],S[S_mfi].min(bx,Temp));
            tdhmin[1] = std::min(tdhmin[1],S[S_mfi].min(bx,Density));
            tdhmin[2] = std::min(tdhmin[2],S[S_mfi].min(bx,RhoH));

            tdhmax[0] = std::max(tdhmax[0],S[S_mfi].max(bx,Temp));
            tdhmax[1] = std::max(tdhmax[1],S[S_mfi].max(bx,Density));
            tdhmax[2] = std::max(tdhmax[2],S[S_mfi].max(bx,RhoH));
        }

        const int IOProc = ParallelDescriptor::IOProcessorNumber();

        ParallelDescriptor::ReduceRealMin(tdhmin,3,IOProc);
        ParallelDescriptor::ReduceRealMax(tdhmax,3,IOProc);

        FArrayBox   Y, tmp;
        bool        aNegY = false;
        Array<Real> minY(nspecies,1.e30);

        for (MFIter S_mfi(S); S_mfi.isValid(); ++S_mfi)
        {
            Y.resize(S_mfi.validbox(),1);

            tmp.resize(S_mfi.validbox(),1);
            tmp.copy(S[S_mfi],Density,0,1);
            tmp.invert(1);

            for (int i = 0; i < nspecies; ++i)
            {
                Y.copy(S[S_mfi],first_spec+i,0,1);
                Y.mult(tmp,0,0,1);
                minY[i] = std::min(minY[i],Y.min(0));
            }
        }

        ParallelDescriptor::ReduceRealMin(minY.dataPtr(),nspecies,IOProc);

        for (int i = 0; i < nspecies; ++i)
        {
            if (minY[i] < 0) aNegY = true;
        }

        if (ParallelDescriptor::IOProcessor())
        {
            std::cout << "  Min,max temp = " << tdhmin[0] << ", " << tdhmax[0] << '\n';
            std::cout << "  Min,max rho  = " << tdhmin[1] << ", " << tdhmax[1] << '\n';
            std::cout << "  Min,max rhoh = " << tdhmin[2] << ", " << tdhmax[2] << '\n';
            if (aNegY)
            {
                const Array<std::string>& names = HeatTransfer::getChemSolve().speciesNames();
                std::cout << "  Species w/min < 0: ";
                for (int i = 0; i < nspecies; ++i)
                    if (minY[i] < 0)
                        std::cout << "Y(" << names[i] << ") [" << minY[i] << "]  ";
                std::cout << '\n';
            }
        }
    }
}

void
HeatTransfer::compute_rhoRT (const MultiFab& S,
                             MultiFab&       p, 
                             int             pComp,
                             const MultiFab* temp)
{
    BL_ASSERT(pComp<p.nComp());

    const BoxArray& sgrids = S.boxArray();
    int nCompY = last_spec - first_spec + 1;

    FArrayBox state;

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        const int  i      = mfi.index();
	const Box& box    = sgrids[i];
	const int  sCompR = 0;
	const int  sCompT = 1;
	const int  sCompY = 2;
	
        state.resize(box,nCompY+2);
	BL_ASSERT(S[mfi].box().contains(box));
	state.copy(S[mfi],box,Density,box,sCompR,1);

	if (temp)
	{
	    BL_ASSERT(temp->boxArray()[i].contains(box));
	    state.copy((*temp)[i],box,0,box,sCompT,1);
	}
        else
        {
	    state.copy(S[mfi],box,Temp,box,sCompT,1);
	}
	state.copy(S[mfi],box,first_spec,box,sCompY,nCompY);

        {
            FArrayBox tmp(box,1);
            tmp.copy(state,sCompR,0,1);
            tmp.invert(1);

            for (int k = 0; k < nCompY; k++)
                state.mult(tmp,0,sCompY+k,1);
        }

	getChemSolve().getPGivenRTY(p[i],state,state,state,
				    box,sCompR,sCompT,sCompY,pComp);
    }
}
			   
//
// Setup for the advance function.
//

#ifndef NDEBUG
#if defined(BL_OSF1)
#if defined(BL_USE_DOUBLE)
const Real BL_BOGUS      = DBL_QNAN;
#else
const Real BL_BOGUS      = FLT_QNAN;
#endif
#else
const Real BL_BOGUS      = 1.e30;
#endif
#endif

void
HeatTransfer::advance_setup (Real time,
                             Real dt,
                             int  iteration,
                             int  ncycle)
{
    NavierStokes::advance_setup(time, dt, iteration, ncycle);

    //
    // Make sure the new state has values so that c-n works
    // in the predictor (of the predictor)--rbp.
    //
    for (int k = 0; k < num_state_type; k++)
    {
        MultiFab& nstate = get_new_data(k);
        MultiFab& ostate = get_old_data(k);

        MultiFab::Copy(nstate,ostate,0,0,nstate.nComp(),0);	
    }

    //REMOVE ME debugging
//     {
//       MultiFab& S_old = get_old_data (State_Type);
//       //      VisMF::Write(S_old,"sold_setup");
//       for (MFIter mfi(S_old); mfi.isValid(); ++mfi)
// 	{
// 	  std::ofstream edge_fab("sold.fab");
// 	  S_old[mfi.index()].writeOn(edge_fab);
// 	  edge_fab.close();
// 	}
//     }

//     (*aofs).setVal(0.0);

    make_rho_curr_time();

#ifndef NDEBUG
    aux_boundary_data_old.setVal(BL_BOGUS);
    aux_boundary_data_new.setVal(BL_BOGUS);
#endif
    //
    // Set a dumbbell flag to help avoid stupid mistakes
    //
    for (int i = 0; i < spec_diffusion_flux_computed.size(); ++i)
	spec_diffusion_flux_computed[i] = HT_None;

}

void
HeatTransfer::setThermoPress(Real time)
{
    const TimeLevel whichTime = which_time(State_Type,time);
    
    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);
    
    MultiFab& S = (whichTime == AmrOldTime) ? get_old_data(State_Type) : get_new_data(State_Type);
    
    const int pComp = (have_rhort ? RhoRT : Trac);

    compute_rhoRT (S,S,pComp);
}

Real
HeatTransfer::advance (Real time,
                       Real dt,
                       int  iteration,
                       int  ncycle)
{
  Real dt_test = 0.0;

  if(use_sdc)
    dt_test = advance_sdc(time,dt,iteration,ncycle);
  else 
  {
    if (level == 0)
    {
        crse_dt = dt;
        int thisLevelStep = parent->levelSteps(0);
        FORT_SET_COMMON(&time,&thisLevelStep);
    }

    if (verbose && ParallelDescriptor::IOProcessor())
    {
        std::cout << "Advancing level "    << level
                  << " : starting time = " << time
                  << " with dt = "         << dt << std::endl;
    }

    advance_setup(time,dt,iteration,ncycle);

 //    {
//       MultiFab& S_old = get_old_data (State_Type);
//       VisMF::Write(S_old,"sold_setup");
//       for (MFIter mfi(S_old); mfi.isValid(); ++mfi)
// 	{
// 	  std::ofstream edge_fab("sold.fab");
// 	  S_old[mfi.index()].writeOn(edge_fab);
// 	  edge_fab.close();
// 	}
//     }

    if (do_check_divudt)
        checkTimeStep(dt);
    
    MultiFab& S_new = get_new_data(State_Type);
    MultiFab& S_old = get_old_data(State_Type);
    //    VisMF::Write(S_old,"sold_ht");
//     if(S_old.defined(0))
//       {
// 	std::ofstream edge_fab("sold.fab");
// 	S_old[0].writeOn(edge_fab);
// 	edge_fab.close();
//       }


    //
    // Reset flag that fill patched state data is good
    // this flag affects how the ghost cells get filled if FPOS_ok = false.
    // changes things so that ghost cells go and grab the reacted data
    // that is stored in aux_boundary_data_old
    // for use with strang splitting.  should not be needed with SDC 
    FillPatchedOldState_ok = true;
    //
    // Compute traced states for normal comp of velocity at half time level.
    //
    Real dummy = 0.0;    
    dt_test = predict_velocity(dt,dummy);
    
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): at start of time step\n";

    temperature_stats(S_old);

    const Real prev_time = state[State_Type].prevTime();
    const Real cur_time  = state[State_Type].curTime();
#if 0
    setThermoPress(prev_time);
#endif
    //
    // Do MAC projection and update edge velocities.
    //
    if (do_mac_proj) 
    {
        int havedivu = 1;
        MultiFab* mac_rhs = create_mac_rhs(time,dt);
        mac_project(time,dt,S_old,mac_rhs,havedivu);
        delete mac_rhs;
    }

    if (do_mom_diff == 0)
        velocity_advection(dt);
    //
    // Set old-time boundary data for RhoH
    // 
    set_overdetermined_boundary_cells(time);
    //
    // Advance the old state for a Strang-split dt/2.  Include grow cells in
    // advance, and squirrel these away for diffusion and Godunov guys to
    // access for overwriting non-advanced fill-patched grow cell data.
    //
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... advancing chem\n";

    BL_ASSERT(S_new.boxArray() == S_old.boxArray());

    const int nComp = NUM_STATE - BL_SPACEDIM;
    Array<int> consumptionComps; // Put in scope for work below
    if (plot_consumption)
    {
        //
        // Save off a copy of the pre-chem state
        //
        consumptionComps.resize(consumptionName.size());
        for (int j=0; j<consumptionComps.size(); ++j)
        {
            consumptionComps[j] = getChemSolve().index(consumptionName[j]) + first_spec;
            auxDiag["CONSUMPTION"]->copy(S_old,consumptionComps[j],j,1);
        }
    }

    MultiFab Qtmp; // Put in scope for work below
    if (plot_heat_release)
    {
        //
        // Save off a copy of the pre-chem state
        //
        Qtmp.define(grids,getChemSolve().numSpecies(),0,Fab_allocate);
        MultiFab::Copy(Qtmp,S_old,first_spec,0,Qtmp.nComp(),0);
    }
    //
    // Build a MultiFab parallel to "fabs".  Force it to have the
    // same distribution as aux_boundary_data_old.  This'll cut out a
    // parallel copy.  It doesn't happen by default (like you might think)
    // due to not being recached appropriately after regridding.
    //
    {
        MultiFab tmpFABs;

	if (!aux_boundary_data_old.isEmpty())
	{
	  tmpFABs.define(aux_boundary_data_old.equivBoxArray(),

			 NUM_STATE,
			 0,
			 aux_boundary_data_old.DistributionMap(),
			 Fab_allocate);

	  const int ngrow = aux_boundary_data_old.nGrow();
	
	  {
            BoxArray ba = S_old.boxArray();

            ba.grow(ngrow);
            //
            // This MF is guaranteed to cover tmpFABs & valid region of S_old.
            //
            // Note that S_old & tmpS_old have the same distribution.
            //
            MultiFab tmpS_old(ba,NUM_STATE,0);

            for (FillPatchIterator fpi(*this,S_old,ngrow,prev_time,State_Type,0,NUM_STATE);
                 fpi.isValid();
                 ++fpi)
	      {
                tmpS_old[fpi.index()].copy(fpi());
	      }

            tmpFABs.copy(tmpS_old);
	  }
	  strang_chem(tmpFABs,dt,HT_LeaveYdotAlone,ngrow);
	  
	  aux_boundary_data_old.copyFrom(tmpFABs,BL_SPACEDIM,0,nComp);
	}

        strang_chem(S_old,  dt,HT_LeaveYdotAlone);
    }
    //
    // Find change due to first Strang step.
    //
    if (plot_consumption)
    {
        MultiFab tmp(auxDiag["CONSUMPTION"]->boxArray(),consumptionComps.size(),0);
        tmp.setVal(0);
        for (int j=0; j<consumptionComps.size(); ++j)
        {
            tmp.copy(S_old,consumptionComps[j],j,1);
        }
        for (MFIter mfi(*auxDiag["CONSUMPTION"]); mfi.isValid(); ++mfi)
        {
            (*auxDiag["CONSUMPTION"])[mfi].minus(tmp[mfi],0,0,consumptionComps.size());
        }
    }
    if (plot_heat_release)
    {
        for (MFIter mfi(Qtmp); mfi.isValid(); ++mfi)
        {
            Qtmp[mfi].minus(S_old[mfi],first_spec,0,Qtmp.nComp());
        }
    }

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): after first Strang step\n";

    //    VisMF::Write(S_old,"sold2_ht");

    temperature_stats(S_old);
    //
    // Activate hook in FillPatch hack to get better data now.
    //
    FillPatchedOldState_ok = false;
    //
    // Compute tn coeffs based on chem-advance tn data
    //  (these are used in the Godunov extrapolation)
    //
    const int nScalDiffs = NUM_STATE-BL_SPACEDIM-1;
    // calculate mixture averaged diffusivity (D_m) for each species
    // load into NavierStokes::diffn_cc as Rho*D_m
    // calculate thermal diffusivity; load into NS::diffn_cc
    // calculate enthalpy diffusivity (thermal diffusivity/c_p,mix)
    calcDiffusivity(prev_time,dt,iteration,ncycle,Density+1,nScalDiffs);
    //    VisMF::Write(S_old,"sold_ht");

    //
    // Godunov-extrapolate states to cell edges
    // this function also computes the time n fluxes and from them
    // the diffusion terms at time n, saved in SpecDiffFlux
    compute_edge_states(dt);
    
//     std::ofstream edge_fab("edgex_orig");
//     (*EdgeState[0])[0].writeOn(edge_fab);
//     edge_fab.close();
    
//     std::ofstream edgey_fab("edgey_orig");
//     (*EdgeState[1])[0].writeOn(edgey_fab);
//     edgey_fab.close();

    //
    // Compute advective fluxes divergences, where possible
    // NOTE: If Le!=1 cannot do RhoH advection until after spec update fills
    //       spec diffusion fluxes.  Since it is never a bad idea to do this
    //       later (i.e. S_old does not change), we do it later always.
    //
    const int first_scalar = Density;
    const int last_scalar = first_scalar + NUM_SCALARS - 1;
    bool do_adv_reflux = true;
    // JFG: the flag true means to load the advective flux registers into aofs
    if (RhoH > first_scalar)
	scalar_advection(dt,first_scalar,RhoH-1,do_adv_reflux);
    if (RhoH < last_scalar)
	scalar_advection(dt,RhoH+1,last_scalar,do_adv_reflux);
    /*//REMOVE ME

    RESULT:
    NUM_STATE = 16
    BL_SPACEDIM = 2
    nScalDiffs = 13
    Density = 2
    NUM_SCALARS = 14
    RhoH = 12
    first_scalar = 2
    last_scalar = 15
    desc_list(0): x_velocity y_velocity density rho.Y(H2) rho.Y(H) rho.Y(O) rho.Y(O2) rho.Y(OH) rho.Y(H2O) rho.Y(HO2) rho.Y(H2O2) rho.Y(N2) rhoh tracer temp RhoRT 
    desc_list(1): pressure 
    desc_list(2): divu 
    desc_list(3): dsdt 
    desc_list(4): FuncCount 
    desc_list(5): d[Y(H2)]/dt d[Y(H)]/dt d[Y(O)]/dt d[Y(O2)]/dt d[Y(OH)]/dt d[Y(H2O)]/dt d[Y(HO2)]/dt d[Y(H2O2)]/dt d[Y(N2)]/dt 

    */
    //
    // Copy old-time boundary Density & RhoH into estimate for new-time RhoH.
    //
    aux_boundary_data_new.copy(aux_boundary_data_old,Density-BL_SPACEDIM,0,1);
    aux_boundary_data_new.copy(aux_boundary_data_old,RhoH-BL_SPACEDIM,   1,1);
    //
    // Save rho used in rho-states, needed for replacing with new one
    //  NOTE: WE LOAD/USE GROW CELLS HERE SO WE CAN FIX BOUNDARY DATA AS WELL
    //
    MultiFab Rho_hold(grids,1,LinOp_grow);

    BL_ASSERT(LinOp_grow == 1);

    for (MFIter mfi(*rho_ctime); mfi.isValid(); ++mfi)
    {
        const Box box = BoxLib::grow(mfi.validbox(),LinOp_grow);
        Rho_hold[mfi.index()].copy((*rho_ctime)[mfi],box,0,box,0,1);
    }
    //
    // Compute new and half-time densities.
    //
    const int rho_corr = 1;
    scalar_update(dt,Density,Density,rho_corr);
    //    VisMF::Write(S_new,"rho_update_ht");    
    //
    // Set saved rho at current time.
    //
    make_rho_curr_time();
    //
    // Reset rho-states to contain new rho.
    //
    reset_rho_in_rho_states(Rho_hold,cur_time,first_scalar+1,NUM_SCALARS-1);

    Rho_hold.clear();
    //
    // Compute the update to momentum
    //
    if (do_mom_diff == 1)
	momentum_advection(dt,do_reflux);

    if (do_rk_diffusion)
    {
        //
        // Update energy and species: Runge-Kutta method.
        //

	// finish the advective update by including rho H.
	bool do_adv_reflux = true;
        scalar_advection(dt,RhoH,RhoH,do_adv_reflux);

	// get locations of some things in the state
	int index_of_firstY = Density + 1;
	int index_of_rhoH = RhoH;
	int index_of_T = Temp;

	// get the old and new states
	MultiFab& S_new = get_new_data(State_Type);
	MultiFab& S_old = get_old_data(State_Type);

	// the old state currently holds the time n values plus the half step chemistry
	// apply the diffusion operator to the old state to get fluxes and updates
        // associated with the old state
	MultiFab* div_of_flux_for_H_old;
	MultiFab* div_of_flux_for_Y_old;
	MultiFab** flux_for_H_old;
	MultiFab** flux_for_Y_old;

	rk_diffusion_operator (prev_time,
			       - dt,
			       div_of_flux_for_H_old,
			       div_of_flux_for_Y_old,
			       flux_for_H_old,
			       flux_for_Y_old);

	// the new state currently holds the time n values plus the half step chemistry
	// add the advective terms to the new state
	scalar_advection_update(dt, first_spec, last_spec);
	scalar_advection_update(dt, RhoH, RhoH);

	// save these values.
	MultiFab save_for_rhoH (grids, 1, 0);
	MultiFab save_for_rhoY (grids, nspecies, 0);
	// note the arguments for MultiFab:: procedures are 
	// (dst, src, srccomp, dstcomp, ncomp, nghost);
	MultiFab::Copy (save_for_rhoH, S_new, index_of_rhoH, 0, 1, 0);
	MultiFab::Copy (save_for_rhoY, S_new, index_of_firstY, 0, nspecies, 0);

	// form the new_star state in the new state by adding the updates associated 
        // with the old state
	MultiFab::Add (S_new, *div_of_flux_for_H_old, 0, index_of_rhoH, 1, 0);
	MultiFab::Add (S_new, *div_of_flux_for_Y_old, 0, index_of_firstY, nspecies, 0);

	// apply the diffusion operator to the new_star state to get fluxes and updates
        // associated with the new_star state
	MultiFab* div_of_flux_for_H_new;
	MultiFab* div_of_flux_for_Y_new;
	MultiFab** flux_for_H_new;
	MultiFab** flux_for_Y_new;

	rk_diffusion_operator (cur_time,
			       - dt,
			       div_of_flux_for_H_new,
			       div_of_flux_for_Y_new,
			       flux_for_H_new,
			       flux_for_Y_new);

	// restore the new state with the saved values so that it holds the sum of the old
        // state and the forcing and advection terms.
	MultiFab::Copy (S_new, save_for_rhoH, 0, index_of_rhoH, 1, 0);
	MultiFab::Copy (S_new, save_for_rhoY, 0, index_of_firstY, nspecies, 0);

        save_for_rhoY.clear();
        save_for_rhoH.clear();

	// form the new state in place by adding the average of the updates associated with
        // the old state and the new_star state
	(*div_of_flux_for_H_old).mult (0.5, 0);
	(*div_of_flux_for_Y_old).mult (0.5, 0);
	MultiFab::Add (S_new, *div_of_flux_for_H_old, 0, index_of_rhoH, 1, 0);
	MultiFab::Add (S_new, *div_of_flux_for_Y_old, 0, index_of_firstY, nspecies, 0);
	(*div_of_flux_for_H_new).mult (0.5, 0);
	(*div_of_flux_for_Y_new).mult (0.5, 0);
	MultiFab::Add (S_new, *div_of_flux_for_H_new, 0, index_of_rhoH, 1, 0);
	MultiFab::Add (S_new, *div_of_flux_for_Y_new, 0, index_of_firstY, nspecies, 0);

	// place into the flux registers the average of the old and new H fluxes
	FArrayBox average_flux;
	int components = 1;
	for (int dimension = 0; dimension < BL_SPACEDIM; dimension++)
	{
	    for (MFIter flux_mfi(*flux_for_H_old[dimension]); flux_mfi.isValid(); ++flux_mfi)
	    {
		const Box& edge_box = (*flux_for_H_old[dimension])[flux_mfi].box();
		average_flux.resize(edge_box,components);
		average_flux.copy((*flux_for_H_old[dimension])[flux_mfi],edge_box,0,edge_box,0,components);
		average_flux.plus((*flux_for_H_new[dimension])[flux_mfi],edge_box,0,0,components);
                average_flux.mult(0.5);
		if (level < parent->finestLevel())
		{
		    getLevel(level+1).getViscFluxReg().CrseInit
			(average_flux, edge_box, dimension, 0, index_of_rhoH, components, -dt);
		}
		if (level > 0)
		    getViscFluxReg().FineAdd
			(average_flux, dimension, flux_mfi.index(), 0, index_of_rhoH, components, dt);
	    }
	}

	// place into the flux registers the average of the old and new Y fluxes
	components = nspecies;
	for (int dimension = 0; dimension < BL_SPACEDIM; dimension++)
	{
	    for (MFIter flux_mfi(*flux_for_Y_old[dimension]); flux_mfi.isValid(); ++flux_mfi)
	    {
		const Box& edge_box = (*flux_for_Y_old[dimension])[flux_mfi].box();
		average_flux.resize(edge_box,components);
		average_flux.copy((*flux_for_Y_old[dimension])[flux_mfi],edge_box,0,edge_box,0,components);
		average_flux.plus((*flux_for_Y_new[dimension])[flux_mfi],edge_box,0,0,components);
                average_flux.mult(0.5);
		if (level < parent->finestLevel())
		    getLevel(level+1).getViscFluxReg().CrseInit
			(average_flux, edge_box, dimension, 0, index_of_firstY, components, -dt);
		
		if (level > 0)
		    getViscFluxReg().FineAdd
			(average_flux, dimension, flux_mfi.index(), 0, index_of_firstY, components, dt);
	    }
	}

        average_flux.clear();

	if (level < parent->finestLevel())
	    getLevel(level+1).getViscFluxReg().CrseInitFinish();

	// delete the space for fluxes and updates
	delete div_of_flux_for_H_new;
	delete div_of_flux_for_Y_new;
	delete div_of_flux_for_H_old;
	delete div_of_flux_for_Y_old;
	diffusion->removeFluxBoxesLevel (flux_for_H_new);
	diffusion->removeFluxBoxesLevel (flux_for_Y_new);
	diffusion->removeFluxBoxesLevel (flux_for_H_old);
	diffusion->removeFluxBoxesLevel (flux_for_Y_old);

	// update the temperature in the new state
	// use the old temperature as an initial guess for the Newton iteration.
	MultiFab::Copy (S_new, S_old, index_of_T, index_of_T, 1, 1);
        RhoH_to_Temp(S_new);

	// update the tracer and RhoRT
        int corrector = 1; // this flag just means this is for real
        tracer_update(dt,corrector);
    }
    else if (do_mcdd)
    {
        //
        // Update energy and species: Marc's multicomponent differential diffusion.
        //

        scalar_advection(dt,RhoH,RhoH,do_adv_reflux); // RhoH aofs, already did others
        mcdd_update(time,dt);
    }
    else
    {
        //
        // Update energy and species: original version.
        //

        //
        // Predictor
        //
        int corrector = 0;
        //
        // Set tnp1 coeffs to tn values in first round of predictor
        //
        MultiFab::Copy(*diffnp1_cc,*diffn_cc,0,0,nScalDiffs,diffn_cc->nGrow());

        temp_update(dt,corrector);         // Here, predict n+1 coeffs using n coeffs
        temperature_stats(S_new);
        
        calcDiffusivity(cur_time,dt,iteration,ncycle,Density+1,nScalDiffs);
// 	VisMF::Write(*diffnp1_cc,"Dmn1_ht");
// 	abort();     

	//VisMF::Write(*aofs,"adv_ht");    
	// FIXME debugging
	//	spec_update(time,dt,corrector);
	scalar_advection_update(dt, first_spec, last_spec);
	differential_spec_diffusion_update(dt, corrector);

	//	VisMF::Write(S_new,"spec_update_ht");
        
        set_overdetermined_boundary_cells(time + dt); // RhoH BC's to see new Y's at n+1

        do_adv_reflux = false;
        scalar_advection(dt,RhoH,RhoH,do_adv_reflux); // Get aofs for RhoH now
	//  	VisMF::Write(*aofs,"adv_ht");     
	//  	abort();    
	
	//        rhoh_update(time,dt,corrector);
	scalar_advection_update(dt, RhoH, RhoH);

	//	VisMF::Write(S_new,"sa_ht");     
	scalar_diffusion_update(dt, RhoH, RhoH, corrector);
//    	VisMF::Write(S_new,"rhdiff_ht");     
//    	abort();


        RhoH_to_Temp(S_new);
        temperature_stats(S_new);
        //
        // Corrector
        //
        corrector = 1;
        calcDiffusivity(cur_time,dt,iteration,ncycle,Density+1,nScalDiffs);
        tracer_update(dt,corrector);

	//FIXME debugging
        //spec_update(time,dt,corrector);
	scalar_advection_update(dt, first_spec, last_spec);
//  	VisMF::Write(S_new,"sn_ht");
//  	abort();
	differential_spec_diffusion_update(dt, corrector);
//       VisMF::Write(*SpecDiffusionFluxnp1[0],"specfluxx_ht");
//       VisMF::Write(*SpecDiffusionFluxnp1[1],"specfluxy_ht");
//       VisMF::Write(*SpecDiffusionFluxn[0],"specfluxxn_ht");
//       VisMF::Write(*SpecDiffusionFluxn[1],"specfluxyn_ht");

        
        set_overdetermined_boundary_cells(time + dt);// RhoH BC's to see new Y's at n+1
        
        do_adv_reflux = true;
        scalar_advection(dt,RhoH,RhoH,do_adv_reflux); // Get aofs for RhoH now
        
        rhoh_update(time,dt,corrector);
//     	VisMF::Write(S_new,"rhdiff_ht");     
//  	abort();

        RhoH_to_Temp(S_new); 
    }
    //
    // Second half of Strang-split chemistry (first half done in
    // compute_edge_states) This takes new-time data, and returns new-time
    // data, as well as providing a predicted Ydot for the velocity
    // constraint.  We write the result over the new state, but only care
    // about stuff in the valid region.
    //
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): after scalar_update\n";

    temperature_stats(S_new);

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... advancing chem\n";
    //
    // Adjust chemistry diagnostic before and after reactions.
    //
    if (plot_consumption)
    {
        for (MFIter mfi(*auxDiag["CONSUMPTION"]); mfi.isValid(); ++mfi)
        {
            for (int j=0; j<consumptionComps.size(); ++j)
            {
                (*auxDiag["CONSUMPTION"])[mfi].plus(S_new[mfi],consumptionComps[j],j,1);
            }
        }
    }
    if (plot_heat_release)
    {
        for (MFIter mfi(Qtmp); mfi.isValid(); ++mfi)
        {
            Qtmp[mfi].plus(S_new[mfi],first_spec,0,Qtmp.nComp());
        }
    }

    strang_chem(S_new,dt,HT_EstimateYdotNew);

    if (plot_consumption)
    {
        for (MFIter mfi(*auxDiag["CONSUMPTION"]); mfi.isValid(); ++mfi)
        {
            for (int j=0; j<consumptionComps.size(); ++j)
            {
                (*auxDiag["CONSUMPTION"])[mfi].minus(S_new[mfi],consumptionComps[j],j,1);
            }
            (*auxDiag["CONSUMPTION"])[mfi].mult(1.0/dt);
        }
    }
    if (plot_heat_release)
    {
        FArrayBox enthi, T;
        for (MFIter mfi(Qtmp); mfi.isValid(); ++mfi)
        {
            Qtmp[mfi].minus(S_new[mfi],first_spec,0,Qtmp.nComp());
	    Qtmp[mfi].mult(1.0/dt);

            const Box& box = mfi.validbox();
            T.resize(mfi.validbox(),1);
            T.setVal(298.15);

            enthi.resize(mfi.validbox(),Qtmp.nComp());
            getChemSolve().getHGivenT(enthi,T,box,0,0);

            // Form heat release
            (*auxDiag["HEATRELEASE"])[mfi].setVal(0.);
            for (int j=0; j<Qtmp.nComp(); ++j)
            {
                Qtmp[mfi].mult(enthi,j,j,1);
                (*auxDiag["HEATRELEASE"])[mfi].plus(Qtmp[mfi],j,0,1);
            }
        }
    }
    //
    //  HACK!!  What are we really supposed to do here?
    //  Deactivate hook in FillPatch hack so that old data really is old data again
    //
    FillPatchedOldState_ok = true;

    if (verbose && ParallelDescriptor::IOProcessor())
	std::cout << "HeatTransfer::advance(): after second Strang-split step\n";
    
    temperature_stats(S_new);
    //
    // S appears in rhs of the velocity update, so we better do it now.
    // (be sure to use most recent version of state to get
    // viscosity/diffusivity).
    //
    calcDiffusivity(cur_time,dt,iteration,ncycle,Density+1,nScalDiffs,true);

    //    VisMF::Write(get_new_data(State_Type),"state_orig");
    //
    // Set the dependent value of RhoRT to be the thermodynamic pressure.  By keeping this in
    // the state, we can use the average down stuff to be sure that RhoRT_avg is avg(RhoRT),
    // not ave(Rho)avg(R)avg(T), which seems to give the p-relax stuff in the mac Rhs troubles.
    //
    setThermoPress(cur_time);

    calc_divu(time+dt, dt, get_new_data(Divu_Type));

    if (!NavierStokes::initial_step && level != parent->finestLevel())
    {
        //
        // Set new divu to old div + dt*dsdt_old where covered by fine.
        //
        BoxArray crsndgrids = getLevel(level+1).grids;
        crsndgrids.coarsen(fine_ratio);
            
        MultiFab& divu_new = get_new_data(Divu_Type);
        MultiFab& divu_old = get_old_data(Divu_Type);
        MultiFab& dsdt_old = get_old_data(Dsdt_Type);
            
        for (MFIter mfi(divu_new); mfi.isValid();++mfi)
        {
            for (int i=0; i<crsndgrids.size(); ++i)
            {
                const Box ovlp = crsndgrids[i] & mfi.validbox();
                if (ovlp.ok())
                {
                    divu_new[mfi].copy(dsdt_old[mfi],ovlp,0,ovlp,0,1);
                    divu_new[mfi].mult(dt,ovlp,0,1);
                    divu_new[mfi].plus(divu_old[mfi],ovlp,0,0,1);
                }
            }
        }
    }
        
    calc_dsdt(time, dt, get_new_data(Dsdt_Type));
    if (NavierStokes::initial_step)
        MultiFab::Copy(get_old_data(Dsdt_Type),get_new_data(Dsdt_Type),0,0,1,0);
    //
    // Add the advective and other terms to get velocity (or momentum) at t^{n+1}.
    //
    velocity_update(dt);
    advance_cleanup(dt,iteration,ncycle);
    //
    // Increment rho average.
    //
    if (!initial_step)
    {
        if (level > 0)
        {
            Real alpha = 1.0/Real(ncycle);
            if (iteration == ncycle)
                alpha = 0.5/Real(ncycle);
            incrRhoAvg(alpha);
        }
        //
        // Do a level project to update the pressure and velocity fields.
        //
        level_projector(dt,time,iteration);

        if (level > 0 && iteration == 1) p_avg->setVal(0);
    }
    //
    // Update estimate for allowable time step.
    //
    dt_test = std::min(dt_test, estTimeStep());
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): at end of time step\n";

    temperature_stats(S_new);

//     if(S_new.defined(0))
//       {
// 	std::ofstream edge_fab("snew_ht");
// 	S_new[0].writeOn(edge_fab);
// 	edge_fab.close();
//       }
  }
  
  return dt_test;
  
}

MultiFab*
HeatTransfer::create_mac_rhs (Real time, Real dt)
{
   MultiFab*    dsdt = getDsdt(0,time);
   MultiFab* mac_rhs = getDivCond(0,time);
   for (MFIter dsdtmfi(*dsdt); dsdtmfi.isValid(); ++dsdtmfi)
   {
       (*dsdt)[dsdtmfi].mult(.5*dt);
       (*mac_rhs)[dsdtmfi].plus((*dsdt)[dsdtmfi]);
   }

   delete dsdt;

   if (dt > 0.0) 
   {
       MultiFab  dpdt(grids,1,0);
       calc_dpdt(time,dt,dpdt,u_mac);

       for (MFIter mfi(dpdt); mfi.isValid(); ++mfi)
           (*mac_rhs)[mfi].plus(dpdt[mfi], grids[mfi.index()], 0,0,1);
   }

   return mac_rhs;
}

void
HeatTransfer::reset_rho_in_rho_states (const MultiFab& rho,
                                       Real            time,
                                       const int       sComp,
                                       const int       nComp)
{
    //
    // Divide the given rho from the states with diffusion terms of the
    // form Laplacian_SoverRho and multiply by the new Rho.
    //
    BL_ASSERT(rho.boxArray() == grids);
    //
    // Only do the valid region.
    //
    MultiFab& S = get_data(State_Type,time);

    BL_ASSERT(sComp + nComp <= S.nComp());

    {
        FArrayBox tmp;

        for (MFIter Smfi(S); Smfi.isValid(); ++Smfi)
        {
            tmp.resize(Smfi.validbox(),1);
            tmp.copy(rho[Smfi],0,0,1);
            tmp.invert(1);

            for (int comp = sComp; comp < sComp + nComp; ++comp)
            {
                if (is_diffusive[comp] && diffusionType[comp]==Laplacian_SoverRho)
                {
                    S[Smfi].mult(tmp,     Smfi.validbox(), 0,       comp, 1);
                    S[Smfi].mult(S[Smfi], Smfi.validbox(), Density, comp, 1);
                }
            }
        }
    }

    if (!aux_boundary_data_new.isEmpty())
    { 
      //
      // Now do the same for AuxBoundaryData.
      // This routine should only be called at new time.
      //
      const TimeLevel whichTime = which_time(State_Type,time);

      BL_ASSERT(whichTime == AmrNewTime);

      const int nGrow = LinOp_grow;

      BL_ASSERT(rho.nGrow() >= nGrow);
      //
      // Make a multifab of "rho" on the boxes of the boundary data.
      // Force them to have the same DistributonMapping().
      //
      MultiFab tmpRho;

      tmpRho.define(aux_boundary_data_new.equivBoxArray(),
		    1,
		    0,
		    aux_boundary_data_new.DistributionMap(),
		    Fab_allocate);

      BoxArray bat = rho.boxArray();

      bat.grow(nGrow);
      //
      // This MF is guaranteed to cover tmpRho.
      //
      MultiFab rhoGrow(bat,1,0);

      for (MFIter rmfi(rhoGrow); rmfi.isValid(); ++rmfi)
        rhoGrow[rmfi].copy(rho[rmfi],rho[rmfi].box());

      tmpRho.copy(rhoGrow);  // Parallel copy.

      rhoGrow.clear();

      FArrayBox tmp;
      for (MFIter rmfi(tmpRho); rmfi.isValid(); ++rmfi)
	{
	  BL_ASSERT(rmfi.validbox() == aux_boundary_data_new[rmfi].box());

	  tmp.resize(rmfi.validbox(),1);
	  tmp.copy(tmpRho[rmfi],0,0,1);
	  tmp.invert(1);

	  BL_ASSERT(is_diffusive[RhoH]);
	  BL_ASSERT(diffusionType[RhoH] == Laplacian_SoverRho);

	  aux_boundary_data_new[rmfi].mult(tmp,0,1,1);
	  aux_boundary_data_new[rmfi].mult(aux_boundary_data_new[rmfi],0,1,1);
	}
    }    
}

void
HeatTransfer::set_overdetermined_boundary_cells (Real time)
{
    BL_ASSERT(first_spec == Density+1);

    const TimeLevel whichTime = which_time(State_Type,time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    AuxBoundaryData& rhoh_data = (whichTime == AmrOldTime) ? aux_boundary_data_old : aux_boundary_data_new;

    if (rhoh_data.isEmpty())
      return; 

    const int nGrow = (whichTime == AmrOldTime) ? Godunov::hypgrow() : LinOp_grow;
    //                                                                                                           
    // Build a MultiFab parallel to State with appropriate # of ghost
    // cells built into the FABs themselves to cover rhoh_data.
    //
    BoxArray ba = grids;

    ba.grow(nGrow);

    MultiFab tmpS(ba,1,0);

    const BoxArray& rhoh_BA = rhoh_data.equivBoxArray();

    const int sCompT = 0, sCompY = 1, sCompH = 0;
    //
    // A non-allocated MultiFab on grids for FPI below.
    //
    {
        FArrayBox tmp, rhoh;

        MultiFab t(grids,1,0,Fab_noallocate);

        for (FillPatchIterator T_fpi(*this,t,nGrow,time,State_Type,Temp,1),
                 RhoY_fpi(*this,t,nGrow,time,State_Type,Density,nspecies+1);
             T_fpi.isValid() && RhoY_fpi.isValid();
             ++T_fpi, ++RhoY_fpi)
        {
            FArrayBox& RhoY = RhoY_fpi();

            BL_ASSERT(RhoY.box() == tmpS[RhoY_fpi.index()].box());

            tmp.resize(RhoY.box(),1);
            tmp.copy(RhoY,0,0,1);
            tmp.invert(1);

            std::vector< std::pair<int,Box> > isects = rhoh_BA.intersections(RhoY.box());

            for (int i = 0; i < isects.size(); i++)
            {
                const Box& isect = isects[i].second;

                for (int j = 1; j < nspecies+1; j++)
                    RhoY.mult(tmp,isect,0,j,1);

                rhoh.resize(isect,1);
                getChemSolve().getHmixGivenTY(rhoh,T_fpi(),RhoY,isect,sCompT,sCompY,sCompH);
                rhoh.mult(RhoY,0,0,1);

                tmpS[T_fpi.index()].copy(rhoh);
            }
        }
    }

    const int RhoHcomp = (whichTime == AmrOldTime) ? RhoH-BL_SPACEDIM : 1;

    rhoh_data.copyFrom(tmpS,0,RhoHcomp,1); // Parallel copy.
}

DistributionMapping
HeatTransfer::getFuncCountDM (const BoxArray& bxba, int ngrow)
{
    //
    // Sometimes "mf" is the valid region of the State.
    // Sometimes it's the region covered by AuxBoundaryData.
    // When ngrow>0 were doing AuxBoundaryData with nGrow()==ngrow.
    //
    DistributionMapping rr;
    rr.RoundRobinProcessorMap(bxba.size(),ParallelDescriptor::NProcs());

    MultiFab fctmpnew;
    fctmpnew.define(bxba, 1, 0, rr, Fab_allocate);
    fctmpnew.setVal(1);

    if (ngrow == 0)
    {
        //
        // Working on valid region of state.
        //
        fctmpnew.copy(get_new_data(FuncCount_Type));  // Parallel copy.
    }
    else
    {
        //
        // Can't directly use a parallel copy from FuncCount_Type to fctmpnew.
        //
        MultiFab& FC = get_new_data(FuncCount_Type);

        BoxArray ba = FC.boxArray();
        ba.grow(ngrow);
        MultiFab grownFC(ba, 1, 0);
        grownFC.setVal(1);
                
        for (MFIter mfi(FC); mfi.isValid(); ++mfi)
            grownFC[mfi].copy(FC[mfi]);

        fctmpnew.copy(grownFC);  // Parallel copy.
    }

    int count = 0;
    Array<long> vwrk(bxba.size());
    for (MFIter mfi(fctmpnew); mfi.isValid(); ++mfi)
        vwrk[count++] = static_cast<long>(fctmpnew[mfi].sum(0));

    fctmpnew.clear();

#if BL_USE_MPI
    const int IOProc = ParallelDescriptor::IOProcessorNumber();

    Array<int> nmtags(ParallelDescriptor::NProcs(),0);
    Array<int> offset(ParallelDescriptor::NProcs(),0);

    for (int i = 0; i < vwrk.size(); i++)
        nmtags[rr.ProcessorMap()[i]]++;

    BL_ASSERT(nmtags[ParallelDescriptor::MyProc()] == count);

    for (int i = 1; i < offset.size(); i++)
        offset[i] = offset[i-1] + nmtags[i-1];

    Array<long> vwrktmp = vwrk;

    MPI_Gatherv(vwrk.dataPtr(),
                count,
                ParallelDescriptor::Mpi_typemap<long>::type(),
                vwrktmp.dataPtr(),
                nmtags.dataPtr(),
                offset.dataPtr(),
                ParallelDescriptor::Mpi_typemap<long>::type(),
                IOProc,
                ParallelDescriptor::Communicator());

    if (ParallelDescriptor::IOProcessor())
    {
        //
        // We must now assemble vwrk in the proper order.
        //
        std::vector< std::vector<int> > table(ParallelDescriptor::NProcs());

        for (int i = 0; i < vwrk.size(); i++)
            table[rr.ProcessorMap()[i]].push_back(i);

        int idx = 0;
        for (int i = 0; i < table.size(); i++)
            for (int j = 0; j < table[i].size(); j++)
                vwrk[table[i][j]] = vwrktmp[idx++]; 
    }
    //
    // Send the properly-ordered vwrk to all processors.
    //
    ParallelDescriptor::Bcast(vwrk.dataPtr(), vwrk.size(), IOProc);
#endif

    DistributionMapping res;
    //
    // This call doesn't invoke the MinimizeCommCosts() stuff.
    //
    res.KnapSackProcessorMap(vwrk,ParallelDescriptor::NProcs());

    return res;
}

void
HeatTransfer::strang_chem (MultiFab&  mf,
                           Real       dt,
                           YdotAction Ydot_action,
                           int        ngrow)
{
    //
    // Sometimes "mf" is the valid region of the State.
    // Sometimes it's the region covered by AuxBoundaryData.
    // When ngrow>0 we're doing AuxBoundaryData with nGrow()==ngrow.
    //
    const Real strt_time = ParallelDescriptor::second();
    //
    // I intend that this function be called just prior to the Godunov
    // extrapolation step for species and temperature (i.e. with FillPatched
    // data in the valid and grow cells), or just after other processes to
    // finish the chem evolve  Here, we:
    //
    //  (1) Carry out the Strang-split chemistry advance (for half time step).
    //
    //  (2) [potentially] Estimate, or improve the value of ydot.
    //       For improving ydot, it is assumed that ydot presently holds the
    //       effective changes in mass fraction, in terms of a rate computed
    //       over the second half of the timestep prior.  We can center that
    //       estimate by averaging with the effective rate over this first half
    // Note:
    //   The dt passed in is the full time step for this level ... and
    //   mf is in State_Type ordering, but starts at the scalars.
    //
    const int rho_comp  = Density; // mf and State_Type completely aligned here
    const int dCompYdot = 0;       // first component of Ydot corres. to first_spec
    const int ycomp     = first_spec - Density + rho_comp;
    const int Tcomp     = Temp - Density + rho_comp;

    MultiFab junk, *ydot_tmp = 0;

    if (Ydot_action == HT_ImproveYdotOld)
    {
        MultiFab& ydot_old = get_old_data(Ydot_Type);
  	junk.define(ydot_old.boxArray(),nspecies,0,Fab_allocate);
	ydot_tmp = &junk;
    }
    else if (Ydot_action == HT_EstimateYdotNew)
    {
	ydot_tmp = &get_new_data(Ydot_Type);
    }

    if (hack_nochem)
    {
      if (ParallelDescriptor::IOProcessor())
      std::cout<<"\nDOING NOCHEM HACK!!!\n";
        if (ydot_tmp)
        {
            ydot_tmp->setVal(0,dCompYdot,nspecies);
        }
    }
    else
    {
        Real p_amb, dpdt_factor;
        FORT_GETPAMB(&p_amb, &dpdt_factor);
        const Real Patm = p_amb / P1atm_MKS;

        {
            FArrayBox tmp;
            for (MFIter Smfi(mf); Smfi.isValid(); ++Smfi)
            {
                tmp.resize(Smfi.validbox(),1);
                tmp.copy(mf[Smfi],rho_comp,0,1);
                tmp.invert(1);

                for (int comp = 0; comp < nspecies; ++comp)
                    mf[Smfi].mult(tmp,0,ycomp+comp,1);
            }
        }

        if (ydot_tmp) 
            ydot_tmp->copy(mf,ycomp,dCompYdot,nspecies);

        FArrayBox* chemDiag = 0;

        if (do_not_use_funccount)
        {
	    MultiFab tmp;

            tmp.define(mf.boxArray(), 1, 0, mf.DistributionMap(), Fab_allocate);

            for (MFIter Smfi(mf); Smfi.isValid(); ++Smfi)
            {
                FArrayBox& fb = mf[Smfi];
                const Box& bx = Smfi.validbox();
		FArrayBox& fc = tmp[Smfi];

                if (plot_reactions &&
                    BoxLib::intersect(mf.boxArray(),auxDiag["REACTIONS"]->boxArray()).size() != 0)
                {
                    chemDiag = &( (*auxDiag["REACTIONS"])[Smfi] );
                }

                getChemSolve().solveTransient(fb,fb,fb,fb,fc,bx,ycomp,Tcomp,0.5*dt,Patm,chem_integrator,chemDiag);
            }
            //
            // When ngrow>0 this does NOT properly update FuncCount_Type since parallel
            // copy()s do not touch ghost cells.  We'll ignore this since we're not using
            // the FuncCount_Type anyway.
            //
	    get_new_data(FuncCount_Type).copy(tmp);
        }
        else
        {
            BoxArray ba = mf.boxArray();

            ba.maxSize(max_grid_size_chem);

            if (ba.size() < 2*ParallelDescriptor::NProcs() && max_grid_size_chem >= 16)
                //
                // Let's chop the grids up a bit more.
                // We want to try and level out the chemistry work.
                //
                ba.maxSize(max_grid_size_chem/2);

            DistributionMapping dm = getFuncCountDM(ba,ngrow);

            MultiFab tmp, fcnCntTemp;

            tmp.define(ba, mf.nComp(), 0, dm, Fab_allocate);

            fcnCntTemp.define(ba, 1, 0, dm, Fab_allocate);

            MultiFab diagTemp;
            const bool do_diag = plot_reactions && BoxLib::intersect(ba,auxDiag["REACTIONS"]->boxArray()).size() != 0;
            if (do_diag)
            {
                diagTemp.define(ba, auxDiag["REACTIONS"]->nComp(), 0, dm, Fab_allocate);
                diagTemp.copy(*auxDiag["REACTIONS"]); // Parallel copy
            }

            if (verbose && ParallelDescriptor::IOProcessor())
                std::cout << "*** strang_chem: FABs in tmp MF: " << tmp.size() << std::endl;

            tmp.copy(mf); // Parallel copy.

            for (MFIter Smfi(tmp); Smfi.isValid(); ++Smfi)
            {
                FArrayBox& fb = tmp[Smfi];
                const Box& bx = Smfi.validbox();
                FArrayBox& fc = fcnCntTemp[Smfi];
                chemDiag = (do_diag ? &(diagTemp[Smfi]) : 0);

                getChemSolve().solveTransient(fb,fb,fb,fb,fc,bx,ycomp,Tcomp,0.5*dt,Patm,chem_integrator,chemDiag);
            }

            mf.copy(tmp); // Parallel copy.

            if (do_diag)
            {
                auxDiag["REACTIONS"]->copy(diagTemp); // Parallel copy
            }

            if (ngrow == 0)
            {
                //
                // Working on valid region of state.
                //
                get_new_data(FuncCount_Type).copy(fcnCntTemp); // Parallel copy.
            }
            else
            {
                //
                // Can't directly use a parallel copy to update FuncCount_Type.
                //
                MultiFab& FC = get_new_data(FuncCount_Type);

                BoxArray ba = FC.boxArray();
                ba.grow(ngrow);
                MultiFab grownFC(ba, 1, 0);
                
                for (MFIter mfi(FC); mfi.isValid(); ++mfi)
                    grownFC[mfi].copy(FC[mfi]);

                grownFC.copy(fcnCntTemp); // Parallel copy.

                for (MFIter mfi(grownFC); mfi.isValid(); ++mfi)
                    FC[mfi].copy(grownFC[mfi]);
            }
        }

        if (ydot_tmp)
        {
            for (MFIter Smfi(mf); Smfi.isValid(); ++Smfi)
            {
                (*ydot_tmp)[Smfi].minus(mf[Smfi], Smfi.validbox(), ycomp, 0, nspecies);
                (*ydot_tmp)[Smfi].mult(1/(0.5*dt), Smfi.validbox(), 0, nspecies);
            }
        }

        for (MFIter Smfi(mf); Smfi.isValid(); ++Smfi)
            for (int comp = 0; comp < nspecies; ++comp)
                mf[Smfi].mult(mf[Smfi],Smfi.validbox(),rho_comp,ycomp+comp,1);

        if (Ydot_action == HT_ImproveYdotOld)
        {
            BL_ASSERT(ydot_tmp != 0);

            MultiFab& ydot_old = get_old_data(Ydot_Type);

            for (MFIter Ymfi(*ydot_tmp); Ymfi.isValid(); ++Ymfi)
            {
                ydot_old[Ymfi].plus((*ydot_tmp)[Ymfi],Ymfi.validbox(),0,dCompYdot,nspecies);
                ydot_old[Ymfi].mult(0.5,Ymfi.validbox(),dCompYdot,nspecies);
            }
        }
    }

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;

    ParallelDescriptor::ReduceRealMax(run_time,IOProc);

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::strang_chem time: " << run_time << std::endl;
}

void
HeatTransfer::compute_edge_states (Real               dt,
                                   std::vector<bool>* state_comps_to_compute)
{
    //
    // Compute edge states, store internally.  Do this to
    // avoid recomputing these, and to allow inter-equation consistency.  Note,
    // storage order in EdgeState same as in State_Type.
    // NOTE: Ordering is important here, must do rho.Y and Temp BEFORE RhoH and
    //       Density, but then it doesn't matter.
    //

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... computing edge states\n";
    //
    // Get simulation parameters.
    //
    const Real* dx             = geom.CellSize();
    const Real  prev_time      = state[State_Type].prevTime();
    const Real  prev_pres_time = state[Press_Type].prevTime();
    //
    // NOTE: Massive memory bloat here...how many MultiFab can we waste??
    // Get viscous forcing on the ones we need, don't bother allocating others
    //
    const int nState = desc_lst[State_Type].nComp();
    PArray<MultiFab> visc_terms(nState,PArrayManage);
    const int use_forces_in_trans = godunov->useForcesInTrans();
    //
    // "do_predict" states are predicted normally, after special states
    //
    std::vector<bool> do_predict(nState,true);

    //REMOVE ME
    //CEG: use_forcces_in_trans = 0
    //     do mom diff = 0
    //     do set rho to species sum = 1

    if (do_mom_diff != 1) 
    {
        for (int d=0; d<BL_SPACEDIM; ++d)
            do_predict[Xvel+d] = false;
    } 
    for (int sigma=first_spec; sigma<=last_spec; ++sigma)
        do_predict[sigma] = false; // Will get these in a special way
    
    if (do_set_rho_to_species_sum)
    {
        do_predict[Density] = false;
        do_predict[RhoH]    = false;
    }

    if (do_mcdd)
    {
        do_predict[RhoH] = false; // Wont need this
        do_predict[Temp] = true; // Will get this in a special way
    }
    //
    // This logic and the associated array passed in allows the computation
    // of the edge states to be shut off for specific components.  This is
    // intended to allow special components, such as RhoK and RhoEps in
    // TurbHT be treated differently.  This logic tries to insure that
    // all components with interdependencies are turned on at the same time.
    //
    // CEG: if i use this comp-edge-states, then need to change this logic
    // so Temp doesn't need to get recomputed too.
    std::vector<bool> compute_comp(nState, true);

    if (state_comps_to_compute != 0)
    {
        BL_ASSERT(state_comps_to_compute->size() == nState);

        for (int cmp = 0; cmp < nState; cmp++)
            compute_comp[cmp] = (*state_comps_to_compute)[cmp];

        if (compute_comp[Density] || compute_comp[Temp] ||
            compute_comp[RhoH]    || compute_comp[first_spec])
        {
            BL_ASSERT(compute_comp[Density]);
            BL_ASSERT(compute_comp[Temp]);
            BL_ASSERT(compute_comp[RhoH]);

            for (int sigma=first_spec; sigma<=last_spec; ++sigma)
                BL_ASSERT(compute_comp[sigma]);
        }
    }
    // REMOVE ME
    // If !do_predict, but will need visc terms, get them explicity here
    //
    //CEG: compute_comp[i] = 1
    // do_predict[0-12] = 0
    // do_predict[13-15] = 1

    const int nGrowF = 1;

    MultiFab* divu_fp = create_mac_rhs_grown(nGrowF,prev_time,dt);

    MultiFab Gp;

    if (use_forces_in_trans || (do_mom_diff == 1))
    {
        visc_terms.set(Xvel, new MultiFab(grids,BL_SPACEDIM,nGrowF));
        getViscTerms(visc_terms[Xvel],Xvel,BL_SPACEDIM,prev_time);

        Gp.define(grids,BL_SPACEDIM,1,Fab_allocate);
        getGradP(Gp, prev_pres_time);
    }

    if (compute_comp[first_spec])
    {
        visc_terms.set(first_spec, new MultiFab(grids,nspecies,nGrowF));

        if (do_mcdd)
        {
            visc_terms.set(Temp, new MultiFab(grids,1,nGrowF));
            compute_mcdd_visc_terms(visc_terms[first_spec],0,visc_terms[Temp],0,
                                    prev_time,nGrowF,DDOp::DD_Temp);
        }
        else
        {
            getViscTerms(visc_terms[first_spec],first_spec,nspecies,prev_time);
        }
    }

    //    VisMF::Write(visc_terms[first_spec],"visc_ht");

    //
    // Get all the normal visc terms for everything but velocity
    //
    for (int sigma=BL_SPACEDIM; sigma<nState; ++sigma)
    {
        if (do_predict[sigma] && compute_comp[sigma])
        {
            BL_ASSERT( sigma < first_spec || sigma > last_spec );
            visc_terms.set(sigma, new MultiFab(grids,1,nGrowF));
            if (be_cn_theta == 1.0)
            {
                visc_terms[sigma].setVal(0.0,0,1,nGrowF);
            }
            else
            {
                getViscTerms(visc_terms[sigma],sigma,1,prev_time);
            }
        }
    }
 
    //
    // Loop on grids, and compute edge fluxes
    //
    FArrayBox edge[BL_SPACEDIM],Rho,U,state;
    //
    // FillPatch'd state data.
    //
    for (FillPatchIterator S_fpi(*this,*divu_fp,Godunov::hypgrow(),prev_time,State_Type,0,nState);
         S_fpi.isValid();
         ++S_fpi)
    {
        //
        // Gonna need this array on a per-grid basis
        //
        std::vector<bool> this_edge_state_computed(nState,false);

        const int i = S_fpi.index();

        FArrayBox tforces, tvelforces;

        Rho.resize(S_fpi().box(),1);
        U.resize(S_fpi().box(),BL_SPACEDIM);

        Rho.copy(S_fpi(),Density,0,1);
        U.copy(S_fpi(),Xvel,0,BL_SPACEDIM);
        //
        // Get the spec forces based on CC data (forces on EC data in getViscTerms)
        //
        if (use_forces_in_trans || (do_mom_diff == 1))
        {
            NavierStokes::getForce(tvelforces,i,nGrowF,Xvel,BL_SPACEDIM,
#ifdef GENGETFORCE
				   prev_time,
#endif		 
				   Rho);
            godunov->Sum_tf_gp_visc(tvelforces,visc_terms[Xvel][i],Gp[i],Rho);
        }
        //
        // Set up the workspace for the godunov Box (also resize "edge" for later)
        //
        Array<int> u_bc[BL_SPACEDIM];
        D_TERM(u_bc[0] = getBCArray(State_Type,i,0,1);,
               u_bc[1] = getBCArray(State_Type,i,1,1);,
               u_bc[2] = getBCArray(State_Type,i,2,1);)

        godunov->Setup(grids[i], dx, dt, 0,
                       edge[0], u_bc[0].dataPtr(),
                       edge[1], u_bc[1].dataPtr(),
#if (BL_SPACEDIM == 3)
                       edge[2], u_bc[2].dataPtr(),
#endif
                       U, Rho, tvelforces);

        const int velpred = 0; // Already have edge velocities for transverse derivative

        if (do_mom_diff == 1) 
        {
            FArrayBox vel(S_fpi().box(),BL_SPACEDIM);

            vel.copy(S_fpi(),0,0,BL_SPACEDIM);
            //
            // Loop over the velocity components.
            //
            for (int comp = 0 ; comp < BL_SPACEDIM ; comp++ )
            {
                if (predict_mom_together == 1) 
                {
                    vel.mult(Rho,S_fpi().box(),S_fpi().box(),0,comp,1);
                    tvelforces.mult(Rho,tvelforces.box(),tvelforces.box(),0,comp,1);
                }
                Array<int> bc = getBCArray(State_Type,i,comp,1);

                int iconserv_dummy = 0;
                FArrayBox divu_dummy;

                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM == 3)             
                                     u_mac[2][i], edge[2],
#endif
                                     U,vel,tvelforces,divu_dummy,
                                     comp,comp,bc.dataPtr(),
                                     iconserv_dummy,PRE_MAC);

                for (int d=0; d<BL_SPACEDIM; ++d)
                    (*EdgeState[d])[i].copy(edge[d],0,comp,1);

                this_edge_state_computed[comp] = true;
            }
        }
        tvelforces.clear();
        //
        // Get spec edge states
        // FIXME: Fab copy reqd, force sum below pulls state and forces from same comp
        //
        if (compute_comp[first_spec])
        {
            FArrayBox spec(S_fpi().box(),nspecies);
            spec.copy(S_fpi(),first_spec,0,nspecies);

            NavierStokes::getForce(tforces,i,nGrowF,first_spec,nspecies,
#ifdef GENGETFORCE
				   prev_time,
#endif		 
				   Rho);

            for (int comp = 0 ; comp < nspecies ; comp++)
            {
                int state_ind = first_spec + comp;
                int use_conserv_diff = 
                      (advectionType[state_ind] == Conservative) ? true : false;
                Array<int> bc = getBCArray(State_Type,i,state_ind,1);

                AdvectionScheme adv_scheme = FPU;
                if (adv_scheme == PRE_MAC)
                {
                    godunov->Sum_tf_divu_visc(spec, tforces, comp, 1,
                                              visc_terms[first_spec][i], comp,
                                              (*divu_fp)[i], Rho, use_conserv_diff);
                    
                    int iconserv_dummy = 0;
                    godunov->edge_states(grids[i], dx, dt, velpred,
                                         u_mac[0][i], edge[0],
                                         u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                         u_mac[2][i], edge[2],
#endif
                                         U,spec,tforces,(*divu_fp)[i],
                                         comp,state_ind,bc.dataPtr(),
                                         iconserv_dummy,PRE_MAC);
                }
                else
                {
                    FArrayBox junkDivu(tforces.box(),1);
                    junkDivu.setVal(0.);
		    //CEG: just need to change tforces
		    // Sum... tforces = visc_terms + tforces - div_u
                    godunov->Sum_tf_divu_visc(spec, tforces, comp, 1,
                                              visc_terms[first_spec][i], comp,
                                              junkDivu, Rho, use_conserv_diff);
                    
                    godunov->edge_states(grids[i], dx, dt, velpred,
                                         u_mac[0][i], edge[0],
                                         u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                         u_mac[2][i], edge[2],
#endif
                                         U,spec,tforces,(*divu_fp)[i],
                                         comp,state_ind,bc.dataPtr(), 
                                         use_conserv_diff,FPU);
                }

                for (int d=0; d<BL_SPACEDIM; ++d)
                    (*EdgeState[d])[i].copy(edge[d],0,state_ind,1);

                this_edge_state_computed[state_ind] = true;
            }
        }
        //
        // Get density edge states
        //
        if (compute_comp[Density])
        {
            if (do_set_rho_to_species_sum)
            {
                for (int d=0; d<BL_SPACEDIM; ++d)
                {
                    (*EdgeState[d])[i].setVal(0.0,edge[d].box(),Density,1);
                    for (int sigma=first_spec; sigma<=last_spec; ++sigma)
                        (*EdgeState[d])[i].plus((*EdgeState[d])[i],
                                                edge[d].box(), sigma,Density,1);
                }
                this_edge_state_computed[Density] = true;
            }
            else
            {
                BoxLib::Error("No code yet for rho != sum(rho.Y)");
            }

            if (do_mom_diff == 1 && predict_mom_together == 0)
               for (int icomp = 0; icomp < BL_SPACEDIM; icomp++)
                  for (int d=0; d<BL_SPACEDIM; ++d)
                    (*EdgeState[d])[i].mult((*EdgeState[d])[i],(*EdgeState[d])[i].box(),
                                            (*EdgeState[d])[i].box(),Density,icomp,1);
        }

        if (compute_comp[Temp])
        {
            //
            // Get Temp edge states via extrap.
            //
            const int comp = 0;
            const int state_ind = Temp;
            int use_conserv_diff = 
                      (advectionType[state_ind] == Conservative) ? true : false;
            state.resize(S_fpi().box(),1);
            state.copy(S_fpi(),state_ind,0,1);

            NavierStokes::getForce(tforces,i,nGrowF,state_ind,1,
#ifdef GENGETFORCE
				   prev_time,
#endif		 

				   Rho);
            Array<int> bc = getBCArray(State_Type,i,state_ind,1);

            AdvectionScheme adv_scheme = FPU;

            if (adv_scheme == PRE_MAC)
            {
                godunov->Sum_tf_divu_visc(state, tforces,  comp, 1,
                                          visc_terms[state_ind][i], 0,
                                          (*divu_fp)[i], Rho, use_conserv_diff);

                int iconserv_dummy = 0;
                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                     u_mac[2][i], edge[2],
#endif
                                     U, state, tforces, (*divu_fp)[i],
                                     comp, state_ind, bc.dataPtr(),
                                     iconserv_dummy, PRE_MAC);

            }
            else
            {

                FArrayBox junkDivu(tforces.box(),1);
                junkDivu.setVal(0.);
                godunov->Sum_tf_divu_visc(state, tforces,  comp, 1,
                                          visc_terms[state_ind][i], 0,
                                          junkDivu, Rho, use_conserv_diff);

                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                     u_mac[2][i], edge[2],
#endif
                                     U, state, tforces, (*divu_fp)[i],
                                     comp, state_ind, bc.dataPtr(), 
                                     use_conserv_diff, FPU);
            }

            for (int d=0; d<BL_SPACEDIM; ++d)
                (*EdgeState[d])[i].copy(edge[d],0,state_ind,1);

            this_edge_state_computed[state_ind] = true;
        }

        if (compute_comp[RhoH])
        {
            //
            // Set rhoh on edges = sum(rho.Y.H)
            //
            FArrayBox h;
            for (int d=0; d<BL_SPACEDIM; ++d)
            {
                (*EdgeState[d])[i].setVal(0.0,edge[d].box(),RhoH,1);
                h.resize(edge[d].box(),nspecies);
                getChemSolve().getHGivenT(h,(*EdgeState[d])[i],
                                          edge[d].box(),Temp,0);
                h.mult((*EdgeState[d])[i],edge[d].box(),first_spec,0,
                       nspecies);
                
                (*EdgeState[d])[i].setVal(0.0,edge[d].box(),RhoH,1);
                for (int comp=0; comp<nspecies; ++comp)
                    (*EdgeState[d])[i].plus(h,edge[d].box(),comp,RhoH,1);
            }
            this_edge_state_computed[RhoH] = true;
        }
        //
        // Now do the rest as normal
        //
        state.resize(S_fpi().box(),1);
        for (int state_ind=0; state_ind<nState; ++state_ind)
        {
            if (do_predict[state_ind]                &&
                !this_edge_state_computed[state_ind] &&
                compute_comp[state_ind])
            {
                int use_conserv_diff =
                    (advectionType[state_ind] == Conservative) ? true : false;
                //
                // Do it the old-fashioned way.
                //
                state.copy(S_fpi(),state_ind,0,1);
                const int comp = 0;
                NavierStokes::getForce(tforces,i,nGrowF,state_ind,1,
#ifdef GENGETFORCE
				       prev_time,
#endif		 
				       Rho);
                godunov->Sum_tf_divu_visc(state, tforces, comp, 1,
                                          visc_terms[state_ind][i], 0,
                                          (*divu_fp)[i], Rho,
                                          use_conserv_diff);
                Array<int> bc = getBCArray(State_Type,i,state_ind,1);
                int iconserv_dummy = 0;
                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                     u_mac[2][i], edge[2],
#endif
                                     U,state,tforces,(*divu_fp)[i],
                                     comp,state_ind,bc.dataPtr(),
                                     iconserv_dummy,PRE_MAC);

                for (int d=0; d<BL_SPACEDIM; ++d)
                    (*EdgeState[d])[i].copy(edge[d],0,state_ind,1);

                this_edge_state_computed[state_ind] = true;
            }
        }
	//CEG:: all edge states except the velocity ones are comoputed here

    }

    delete divu_fp;
}

void
HeatTransfer::momentum_advection (Real dt, bool do_adv_reflux)
{
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... advect momentum\n";

    const int   finest_level   = parent->finestLevel();
    Box edge_bx[BL_SPACEDIM];
    FArrayBox edge[BL_SPACEDIM], area[BL_SPACEDIM], volume;
    //
    // Compute the advective forcing for momentum.
    //
    int use_conserv_diff = true;

    for (MFIter AofS_mfi(*aofs); AofS_mfi.isValid(); ++AofS_mfi)
    {
        const int i = AofS_mfi.index();

        for (int d=0; d<BL_SPACEDIM; ++d)
        {
            edge_bx[d] = BoxLib::surroundingNodes(grids[i],d);
            edge[d].resize(edge_bx[d],1);
        }

        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            geom.GetFaceArea(area[dir],grids,i,dir,GEOM_GROW);
        }
        geom.GetVolume(volume,grids,i,GEOM_GROW);

        for (int comp = 0 ; comp < BL_SPACEDIM ; comp++ )
        {
            // 
            // If here, edge states at n+1/2 have already been computed, get a copy
            // 
            for (int d=0; d<BL_SPACEDIM; ++d)
                edge[d].copy((*EdgeState[d])[i],edge_bx[d],comp,edge_bx[d],0,1);
 
            godunov->ComputeAofs(grids[i],
                                 area[0],u_mac[0][i],edge[0],
                                 area[1],u_mac[1][i],edge[1],
#if BL_SPACEDIM==3
                                 area[2],u_mac[2][i],edge[2],
#endif
                                 volume,(*aofs)[i],comp,
                                 use_conserv_diff);
            //
            // Get fluxes for diagnostics and refluxing.
            //
            pullFluxes(i, comp, 1, edge[0], edge[1], edge[2], dt);
        }
    }

    D_TERM(area[0].clear();, area[1].clear();, area[2].clear(););
    D_TERM(edge[0].clear();, edge[1].clear();, edge[2].clear(););
    //
    // pullFluxes() contains CrseInit() calls -- complete the process.
    //
    if (do_adv_reflux && level < finest_level)
        getAdvFluxReg(level+1).CrseInitFinish();
}

void
HeatTransfer::scalar_advection (Real dt,
                                int  fscalar,
                                int  lscalar,
                                bool do_adv_reflux)
{
    //
    // Compute the advection flux divergences
    //
    const bool do_special_rhoh = nspecies>0 
        && do_set_rho_to_species_sum 
        && fscalar>=RhoH && lscalar<=RhoH
        && !unity_Le
      //CEG:: add_nonunity... = true 
        && do_add_nonunityLe_corr_to_rhoh_adv_flux
        && !do_mcdd
        && !do_rk_diffusion;
    MultiFab** fluxNULN;
    //
    // If RhoH included, compute non-unity Lewis number flux addition.stuff
    // (using LinOp stuff which works on MultiFabs, so need to do this prior
    // to the subsequent MFIter loop)
    //
    // Note that this requires reasonable species and Temperature values
    //  in the state at np1, and pulls transport coeffs from the level. 
    //  It should be that the coeffs in the level have been computed with the
    //  temp and species in the state (to preserve isothermal flow over time step)
    //
    if (do_special_rhoh)
    {
        MultiFab Soln(grids,1,1);
        const Real prev_time = state[State_Type].prevTime();
        const Real cur_time  = state[State_Type].curTime();
        MultiFab& S_new = get_new_data(State_Type);
        MultiFab& S_old = get_old_data(State_Type);
        //
        // For each species, make a LinOp that can compute -(lambda/cp).A.Grad(Y_l)
        // (the ViscBndry construction knows how to fill bc's correctly, so
        //    re-construct linop/bndrydata for each species)
        //
        const Real a = 1.0;     // Passed around, but not used
        Real rhsscale;          //  -ditto-
        const int rho_flag = 2; // FIXME: Messy assumption
        MultiFab *alpha=0;      //  -ditto-
        MultiFab **fluxSC, **fluxi, **rhoh_visc;
        diffusion->allocFluxBoxesLevel(fluxSC,0,1);
        diffusion->allocFluxBoxesLevel(fluxi,0,nspecies);
        diffusion->allocFluxBoxesLevel(fluxNULN,0,1);
        diffusion->allocFluxBoxesLevel(rhoh_visc,0,1);

        const int nGrow    = 1; // Size to grow fil-patched fab for T below
        const int dataComp = 0; // coeffs loaded into 0-comp for all species
        //
        // Initialize fluxNULN (NULN = non-unity Lewis number)
        //
        for (int d = 0; d < BL_SPACEDIM; ++d)
            fluxNULN[d]->setVal(0.0);
        //
        // Get the NULN flux contrib from n data
        //
        getDiffusivity(rhoh_visc, prev_time, RhoH, 0, 1);
	
	const MultiFab* Rh = get_rho_half_time();

        for (int comp = 0; comp < nspecies; ++comp)
        {
            const Real b     = 1.0 - be_cn_theta;
            const int  sigma = first_spec + comp;
            //
            // Start by getting lambda/cp.Grad(Y) (note: neg of usual diff flux)
            //
            ViscBndry      visc_bndry;
            ABecLaplacian* visc_op;

            visc_op = diffusion->getViscOp(sigma,a,b,prev_time,visc_bndry,
                                           Rh,rho_flag,&rhsscale,dataComp,
                                           rhoh_visc,alpha);

            visc_op->maxOrder(diffusion->maxOrder());
            MultiFab::Copy(Soln,S_old,sigma,0,1,0);

            for (MFIter Smfi(Soln); Smfi.isValid(); ++Smfi)
                Soln[Smfi].divide(S_old[Smfi],Smfi.validbox(),Density,0,1);

            visc_op->compFlux(D_DECL(*fluxSC[0],*fluxSC[1],*fluxSC[2]),Soln);
            for (int d=0; d < BL_SPACEDIM; ++d)
                fluxSC[d]->mult(-b/geom.CellSize()[d]);
            //
            // Here, get fluxi = (lambda/cp - rho.D)Grad(Y)
            //                 = lambda/cp.Grad(Y) + SpecDiffFlux
            //
            for (int d = 0; d < BL_SPACEDIM; ++d)
            {
                for (MFIter SDF_mfi(*SpecDiffusionFluxn[d]);
                     SDF_mfi.isValid();
                     ++SDF_mfi)
                {
                    const Box& ebox    = SDF_mfi.validbox();
                    FArrayBox& SDF_fab = (*SpecDiffusionFluxn[d])[SDF_mfi];
                    (*fluxi[d])[SDF_mfi].copy(SDF_fab,ebox,comp,ebox,comp,1);
                    (*fluxi[d])[SDF_mfi].plus((*fluxSC[d])[SDF_mfi],ebox,0,comp,1);
                }
            }
            delete visc_op;
        }
        //
        // Multiply fluxi by h_i, and add to running total.
        //
        for (FillPatchIterator Told_fpi(*this,S_old,nGrow,prev_time,State_Type,Temp,1);
             Told_fpi.isValid();
             ++Told_fpi)
        {
            const int i    = Told_fpi.index();
            const Box& box = Told_fpi.validbox();

            FArrayBox eTemp, h;

            for (int d = 0; d < BL_SPACEDIM; ++d)
            {
                const Box ebox = BoxLib::surroundingNodes(box,d);
                eTemp.resize(ebox,1);
                FPLoc bc_lo =
                    fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).lo(d));
                FPLoc bc_hi = 
                    fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).hi(d));
                
                center_to_edge_fancy(Told_fpi(),eTemp,
                                     BoxLib::grow(box,BoxLib::BASISV(d)),0,0,1,
                                     geom.Domain(),bc_lo,bc_hi);
                
                h.resize(ebox,nspecies);
                getChemSolve().getHGivenT(h,eTemp,ebox,0,0);
                (*fluxi[d])[i].mult(h,ebox,0,0,nspecies);
                
                for (int comp=0; comp<nspecies; ++comp)
                    (*fluxNULN[d])[i].plus((*fluxi[d])[i],ebox,comp,0,1);
            }
        }
// 	VisMF::Write(*fluxNULN[0],"NULNnx_ht");
// 	VisMF::Write(*fluxNULN[1],"NULNny_ht");
// 	std::cout<<"scalar advection abort"<<std::endl;
// 	abort();

        //
        // Get the Le!=1 flux contrib from n+1 data.
        //

	//FIXME!!! debugging
//  	VisMF::Write(*fluxNULN[0],"NULNx_ht");
//  	VisMF::Write(*fluxNULN[1],"NULNy_ht");

//          for (int d = 0; d < BL_SPACEDIM; ++d)
//              fluxNULN[d]->setVal(0.0);

        getDiffusivity(rhoh_visc, cur_time, RhoH, 0, 1);
       
        for (int comp = 0; comp < nspecies; ++comp)
        {
            const Real b     = be_cn_theta;
            const int  sigma = first_spec + comp;
            //
            //  start by getting lambda/cp.Grad(Y) (note: neg of usual diff flux)
            //
            ViscBndry      visc_bndry;
            ABecLaplacian* visc_op;

            visc_op = diffusion->getViscOp(sigma,a,b,cur_time,visc_bndry,
                                           Rh,rho_flag,&rhsscale,dataComp,
                                           rhoh_visc,alpha);

            visc_op->maxOrder(diffusion->maxOrder());
            MultiFab::Copy(Soln,S_new,sigma,0,1,0);

            for (MFIter Smfi(Soln); Smfi.isValid(); ++Smfi)
                Soln[Smfi].divide(S_new[Smfi],Smfi.validbox(),Density,0,1);

            visc_op->compFlux(D_DECL(*fluxSC[0],*fluxSC[1],*fluxSC[2]),Soln);
            for (int d=0; d < BL_SPACEDIM; ++d)
                fluxSC[d]->mult(-b/geom.CellSize()[d]);
            //
            // Here, get fluxi = (lambda/cp - rho.D)Grad(Y)
            //                 = lambda/cp.Grad(Y) + SpecDiffFlux
            //
            for (int d = 0; d < BL_SPACEDIM; ++d)
            {
                MFIter SDF_mfi(*SpecDiffusionFluxnp1[d]);
                for ( ; SDF_mfi.isValid(); ++SDF_mfi)
                {
                    FArrayBox& SDF_fab = (*SpecDiffusionFluxnp1[d])[SDF_mfi];
                    const Box& ebox    = SDF_mfi.validbox();
                    (*fluxi[d])[SDF_mfi].copy(SDF_fab,ebox,comp,ebox,comp,1);
                    (*fluxi[d])[SDF_mfi].plus((*fluxSC[d])[SDF_mfi],ebox,0,comp,1);
                }
            }
            delete visc_op;
        }
//  	VisMF::Write(*SpecDiffusionFluxnp1[0],"specfluxx_ht");
//  	VisMF::Write(*SpecDiffusionFluxnp1[1],"specfluxy_ht");
	// 	abort();

        Soln.clear();
        diffusion->removeFluxBoxesLevel(fluxSC);
        diffusion->removeFluxBoxesLevel(rhoh_visc);
        //
        // Multiply fluxi by h_i, and add to running total
        //
        for (FillPatchIterator Tnew_fpi(*this,S_new,nGrow,cur_time,State_Type,Temp,1);
             Tnew_fpi.isValid();
             ++Tnew_fpi)
        {
            const int i    = Tnew_fpi.index();
            const Box& box = Tnew_fpi.validbox();

            FArrayBox eTemp, h;

            for (int d = 0; d < BL_SPACEDIM; ++d)
            {
                const Box ebox = BoxLib::surroundingNodes(box,d);
                eTemp.resize(ebox,1);
                FPLoc bc_lo =
                    fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).lo(d));
                FPLoc bc_hi = 
                    fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).hi(d));
                
                center_to_edge_fancy(Tnew_fpi(),eTemp,BoxLib::grow(box,BoxLib::BASISV(d)),
                                     0,0,1,geom.Domain(),bc_lo,bc_hi);
                
                h.resize(ebox,nspecies);
                getChemSolve().getHGivenT(h,eTemp,ebox,0,0);
                (*fluxi[d])[i].mult(h,ebox,0,0,nspecies);

                for (int comp = 0; comp < nspecies; ++comp)
                    (*fluxNULN[d])[i].plus((*fluxi[d])[i],ebox,comp,0,1);
            }
        }

                
//  	VisMF::Write(*fluxNULN[0],"NULN1x_ht");
//  	VisMF::Write(*fluxNULN[1],"NULN1y_ht");
	// 	abort();

	diffusion->removeFluxBoxesLevel(fluxi);
    } //end if(do_special_rhoh)

    Box edge_bx[BL_SPACEDIM];

    FArrayBox edge[BL_SPACEDIM], area[BL_SPACEDIM], volume;

    for (MFIter AofS_mfi(*aofs); AofS_mfi.isValid(); ++AofS_mfi)
    {
        const int i = AofS_mfi.index();

        for (int d=0; d<BL_SPACEDIM; ++d)
        {
            edge_bx[d] = BoxLib::surroundingNodes(grids[i],d);
            edge[d].resize(edge_bx[d],1);
        }
        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            geom.GetFaceArea(area[dir],grids,i,dir,GEOM_GROW);
        }
        geom.GetVolume(volume,grids,i,GEOM_GROW);

        for (int sigma=fscalar; sigma<=lscalar; ++sigma)
        {
            // 
            // If here, edge states at n+1/2 have already been computed, get a copy
            // 
            for (int d=0; d<BL_SPACEDIM; ++d)
                edge[d].copy((*EdgeState[d])[i],edge_bx[d],sigma,edge_bx[d],0,1);
// 	    if (sigma == RhoH){
//  		std::ofstream edge_fab3("edgex1_ht");
// 		edge[0].writeOn(edge_fab3);
// 		edge_fab3.close();
// 		std::ofstream edge_fab4("edgey1_ht");
// 		edge[1].writeOn(edge_fab4);
// 		edge_fab4.close();
// 		std::cout<<"before compute aofs\n";
// 	    }	      
 
            int use_conserv_diff = 
	      (advectionType[sigma] == Conservative) ? true : false;

            godunov->ComputeAofs(grids[i],
                                 area[0],u_mac[0][i],edge[0],
                                 area[1],u_mac[1][i],edge[1],
#if BL_SPACEDIM==3
                                 area[2],u_mac[2][i],edge[2],
#endif
                                 volume,(*aofs)[i],sigma,
                                 use_conserv_diff);
// 	    if (sigma == RhoH)
// 	      VisMF::Write(*aofs,"aofs_ht");
// 	    if (sigma == RhoH){
//  		std::ofstream edge_fab3("edgex2_ht");
// 		edge[0].writeOn(edge_fab3);
// 		edge_fab3.close();
// 		std::ofstream edge_fab4("edgey2_ht");
// 		edge[1].writeOn(edge_fab4);
// 		edge_fab4.close();
// 		std::cout<<"after compute aofs"<<std::endl;
// 		abort();
// 	    }	      

            //
            // Add divergence of fluxNULN to aofs[RhoH], and increment advective
            //  going into flux registers
            //
            if (sigma==RhoH && do_special_rhoh)
            {
	      //REMOVE ME 
	 //      VisMF::Write(*aofs,"adv_ht");
// 	      abort();
    
	      if (do_adv_reflux){
// 		VisMF::Write(*fluxNULN[0],"NULNx_ht");
// 		VisMF::Write(*fluxNULN[1],"NULNy_ht");

//  		std::ofstream edge_fab3("edgextot_ht");
// 		edge[0].writeOn(edge_fab3);
// 		edge_fab3.close();
// 		std::ofstream edge_fab4("edgeytot_ht");
// 		edge[1].writeOn(edge_fab4);
// 		edge_fab4.close();

		for (int d=0; d<BL_SPACEDIM; ++d)
		  edge[d].plus((*fluxNULN[d])[i],edge_bx[d],0,0,1);	
	
// 		std::ofstream edge_fab("edgex_ht");
// 		(*EdgeState[0])[i].writeOn(edge_fab);
// 		edge_fab.close();
// 		std::ofstream edge_fab2("edgey_ht");
// 		(*EdgeState[1])[i].writeOn(edge_fab2);
// 		edge_fab2.close();
//		abort();

	      }

	        FArrayBox& staten = (*aofs)[i];
                const FArrayBox& stateo = staten;
                const Box& box = AofS_mfi.validbox();
                const FArrayBox& vol = volume;
                const Real mult = 1.0; // no dt scaling of aofs, done in scl_adv_upd
                const int nComp = 1;
        
                FORT_INCRWEXTFLXDIV(box.loVect(), box.hiVect(),
                                    (*fluxNULN[0])[i].dataPtr(),
                                    ARLIM((*fluxNULN[0])[i].loVect()),
                                    ARLIM((*fluxNULN[0])[i].hiVect()),
                                    (*fluxNULN[1])[i].dataPtr(),
                                    ARLIM((*fluxNULN[1])[i].loVect()),
                                    ARLIM((*fluxNULN[1])[i].hiVect()),
#if BL_SPACEDIM == 3
                                    (*fluxNULN[2])[i].dataPtr(),
                                    ARLIM((*fluxNULN[2])[i].loVect()),
                                    ARLIM((*fluxNULN[2])[i].hiVect()),
#endif
                                    stateo.dataPtr(RhoH),
                                    ARLIM(stateo.loVect()), ARLIM(stateo.hiVect()),
                                    staten.dataPtr(RhoH),
                                    ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
                                    vol.dataPtr(),
                                    ARLIM(vol.loVect()), ARLIM(vol.hiVect()),
                                    &nComp, &mult);
		//isMF::Write(*aofs,"aofs_ht");
	    }//end if(do_special_rhoh && sigma==RhoH)
	    //
            // Do refluxing
            //
            if (do_adv_reflux)
                pullFluxes(i,sigma,1,edge[0],edge[1],edge[2],dt);
        }
    }

    D_TERM(area[0].clear();, area[1].clear();, area[2].clear(););
    D_TERM(edge[0].clear();, edge[1].clear();, edge[2].clear(););

    if (do_special_rhoh)
        diffusion->removeFluxBoxesLevel(fluxNULN);
    //
    // pullFluxes() contains CrseInit() calls. Got to complete the process.
    //
    if (do_reflux && level < parent->finestLevel())
        getAdvFluxReg(level+1).CrseInitFinish();
}

void
HeatTransfer::rhoh_update (Real time,
                           Real dt,
                           int  corrector) 
{
    //
    // Do implicit c-n solve for RhoH
    //
    scalar_update(dt,RhoH,RhoH,corrector);
}

void
HeatTransfer::temp_update (Real dt,
                           int  corrector) 
{
    //
    // Do implicit c-n solve for temperature.
    //
    scalar_update(dt,Temp,Temp,corrector);
}

void
HeatTransfer::spec_update (Real time,
                           Real dt,
                           int  corrector) 
{
    //
    // Do implicit c-n solve for rho*Y_l, l=0,nspecies-1.
    //

    // compute the advective update.  assumes already have 
    // aofs = div(U S)
    scalar_advection_update(dt, first_spec, last_spec);
	    
    if (unity_Le)
    {
        scalar_diffusion_update(dt, first_spec, last_spec, corrector);
    }
    else
    {
        differential_spec_diffusion_update(dt, corrector);
    }
    //
    // Enforce sum_l rho U Y_l equals rho.
    // CEG:: floor_species = 0
    if (floor_species)
        scale_species(get_new_data(State_Type),0,1);
}

void
HeatTransfer::tracer_update (Real dt,
                             int  corrector) 
{
    //
    // Update tracer.
    //
    if (have_trac)
        scalar_update(dt,Trac,Trac,corrector);
    if (have_rhort)
        scalar_update(dt,RhoRT,RhoRT,corrector);
}

void
HeatTransfer::scalar_update (Real dt,
                             int  first_scalar, 
                             int  last_scalar,
                             int  corrector)
{
    //
    // Do implicit c-n solve for an arbitrary scalar (i.e., not velocity).
    //
    scalar_advection_update(dt, first_scalar, last_scalar);
    scalar_diffusion_update(dt, first_scalar, last_scalar, corrector);
}

//
// sum_l rho D grad Y_l dot grad h_l on the valid region of the multifab.
//
void
HeatTransfer::compute_rhoDgradYgradH (Real      time,
                                      MultiFab& rdgydgh)
{
    //
    // FIXME: Shouldn't this really be the species diffusion fluxes
    //        dotted with grad(h_i) to be consistent? I dunno....
    //
    // Get edge-centered rho.D
    // (copy spec visc from internal database directly (before inflow "zeroing"))
    //
    MultiFab** beta;
    diffusion->allocFluxBoxesLevel(beta,0,nspecies);
    getDiffusivity(beta, time, first_spec, 0, nspecies);
    //
    // Get result, using cell-centered Y,h and edge-centered rhoD
    //
    FArrayBox rdgydgh_spec_i, tmp, h;

    const Real* dx = geom.CellSize();

    rdgydgh.setVal(0,0);
    //
    // nspecies = number of species, ncomp is one greater due to fillpatching
    // density and species together.
    //
    int nspecies = last_spec - first_spec + 1;

    for (FillPatchIterator rho_and_species_fpi(*this,rdgydgh,1,time,State_Type,Density,nspecies+1),
             Temp_fpi(*this,rdgydgh,1,time,State_Type,Temp,1);
         rho_and_species_fpi.isValid() && Temp_fpi.isValid();
         ++rho_and_species_fpi, ++Temp_fpi)
    {
        const int  i               = rho_and_species_fpi.index();
        const int* lo              = grids[i].loVect();
        const int* hi              = grids[i].hiVect();
        FArrayBox& rho_and_species = rho_and_species_fpi();
        const Box& bx              = rho_and_species.box();

        rdgydgh_spec_i.resize(grids[i],1);
        DEF_LIMITS(rdgydgh_spec_i,prod,prodlo,prodhi);

        const  int* speclo  = rho_and_species.loVect();
        const  int* spechi  = rho_and_species.hiVect();

        tmp.resize(rho_and_species.box(),1);
        tmp.copy(rho_and_species,0,0,1);
        tmp.invert(1);

        h.resize(bx,nspecies);

	getChemSolve().getHGivenT(h,Temp_fpi(),bx,0,0);

        for (int spec = first_spec; spec <= last_spec; spec++) 
        {
            const int comp = spec - first_spec;

            DEF_CLIMITSCOMP(h,hdat,hlo,hhi,comp);
            DEF_CLIMITSCOMP((*beta[0])[i],betax,betaxlo,betaxhi,comp);
            DEF_CLIMITSCOMP((*beta[1])[i],betay,betaylo,betayhi,comp);
#if (BL_SPACEDIM==3)
            DEF_CLIMITSCOMP((*beta[2])[i],betaz,betazlo,betazhi,comp);
#endif

            rho_and_species.mult(tmp,0,comp+1,1);

            const Real* specdat = rho_and_species.dataPtr(comp+1);

            FORT_COMPUTE_RHODGRADHDOTGRADY(dx,lo,hi,
                                           ARLIM(speclo),ARLIM(spechi),specdat,
                                           ARLIM(hlo),ARLIM(hhi),hdat,
                                           ARLIM(betaxlo),ARLIM(betaxhi),betax,
                                           ARLIM(betaylo),ARLIM(betayhi),betay,
#if (BL_SPACEDIM==3) 
                                           ARLIM(betazlo),ARLIM(betazhi),betaz,
#endif            
                                           ARLIM(prodlo),ARLIM(prodhi),prod);

            rdgydgh[i].plus(rdgydgh_spec_i);
        }
    }
  
    diffusion->removeFluxBoxesLevel(beta);
}

//
// An enum to clean up mac_sync...questionable usefullness
//
enum SYNC_SCHEME {ReAdvect, UseEdgeState, Other};

void
HeatTransfer::mac_sync ()
{
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... mac_sync\n";

    const Real strt_time = ParallelDescriptor::second();

    int        sigma;
    const int  finest_level   = parent->finestLevel();
    const int  ngrids         = grids.size();
    const Real prev_time      = state[State_Type].prevTime();
    const Real cur_time       = state[State_Type].curTime();
    const Real prev_pres_time = state[Press_Type].prevTime();
    const Real dt             = parent->dtLevel(level);
    MultiFab*  DeltaSsync     = 0; // hold (Delta rho)*q for conserved quantities
    MultiFab*  Rh             = get_rho_half_time();

    sync_setup(DeltaSsync);
    //
    // Compute the correction velocity.
    //
    mac_projector->mac_sync_solve(level,dt,Rh,fine_ratio);
    //
    // Update coarse grid state by adding correction from mac_sync solve.
    //
    if (do_reflux)
    {
        MultiFab& S_new = get_new_data(State_Type);

	Array<SYNC_SCHEME> sync_scheme(NUM_STATE,ReAdvect);

        if (do_mom_diff == 1)
          for (int i=0; i<BL_SPACEDIM; ++i)
            sync_scheme[i] = UseEdgeState;

        for (int i=BL_SPACEDIM; i<NUM_STATE; ++i)
            sync_scheme[i] = UseEdgeState;
        
        Array<int> incr_sync(NUM_STATE,0);
        for (int i=0; i<sync_scheme.size(); ++i)
            if (sync_scheme[i] == ReAdvect)
                incr_sync[i] = 1;

        if (do_mom_diff == 0) 
        {
            mac_projector->mac_sync_compute(level,u_mac,Vsync,Ssync,Rh,
                                            (level > 0) ? &getAdvFluxReg(level) : 0,
                                            advectionType,prev_time,
                                            prev_pres_time,dt,NUM_STATE,
                                            be_cn_theta,
                                            modify_reflux_normal_vel,
                                            do_mom_diff,
                                            incr_sync);
        }
        else
        {
            for (int comp=0; comp<BL_SPACEDIM; ++comp)
            {
                if (sync_scheme[comp]==UseEdgeState)
                {
                    mac_projector->mac_sync_compute(level,Vsync,comp,
                                                    comp,EdgeState, comp,Rh,
                                                    (level>0 ? &getAdvFluxReg(level):0),
                                                    advectionType,modify_reflux_normal_vel,dt);
                }
            }
        }

        for (int comp=BL_SPACEDIM; comp<NUM_STATE; ++comp)
        {
            if (sync_scheme[comp]==UseEdgeState)
            {
                int s_ind = comp - BL_SPACEDIM;
                //
                // This routine does a sync advect step for a single 
                // scalar component. The half-time edge states are passed in.
                // This routine is useful when the edge states are computed
                // in a physics-class-specific manner. (For example, as they are
                // in the calculation of div rho U h = div U sum_l (rho Y)_l h_l(T)).
                //
                mac_projector->mac_sync_compute(level,Ssync,comp,s_ind,
                                                EdgeState,comp,Rh,
                                                (level>0 ? &getAdvFluxReg(level):0),
                                                advectionType,modify_reflux_normal_vel,dt);
            }
        }
        
        Ssync->mult(dt,Ssync->nGrow());
        //
        // For all conservative variables Q (other than density)
        // express Q as rho*q and increment sync by -(sync_for_rho)*q 
        //
        {
            FArrayBox delta_ssync;
            for (MFIter mfi(S_new); mfi.isValid(); ++mfi)
            {
                const int  i   = mfi.index();
                const Box& grd = grids[i];

                int iconserved = -1;

                for (int istate = BL_SPACEDIM; istate < NUM_STATE; istate++)
                {
                    if (istate != Density && advectionType[istate] == Conservative)
                    {
                        iconserved++;
                        
                        delta_ssync.resize(grd,1);
                        delta_ssync.copy(S_new[i],grd,istate,grd,0,1);
                        delta_ssync.divide(S_new[i],grd,Density,0,1);
                        FArrayBox& s_sync = (*Ssync)[i];
                        delta_ssync.mult(s_sync,grd,Density-BL_SPACEDIM,0,1);
                        (*DeltaSsync)[i].copy(delta_ssync,grd,0,grd,iconserved,1);
                        s_sync.minus(delta_ssync,grd,0,istate-BL_SPACEDIM,1);
                    }
                }
            }
        }
        //
        // Now, increment density.
        //
        for (MFIter mfi(S_new); mfi.isValid(); ++mfi)
        {
            const int i = mfi.index();
            S_new[i].plus((*Ssync)[i],grids[i],Density-BL_SPACEDIM,Density,1);
        }
        make_rho_curr_time();

        const int numscal = NUM_STATE - BL_SPACEDIM;
        //
        // Set do_diffuse_sync to 0 for debugging reasons only.
        //
        if (do_mom_diff == 1)
        {
            for (MFIter Vsyncmfi(*Vsync); Vsyncmfi.isValid(); ++Vsyncmfi)
            {
                const int i    = Vsyncmfi.index();
                const Box vbox = (*rho_ctime).box(i);

                D_TERM((*Vsync)[i].divide((*rho_ctime)[i],vbox,0,Xvel,1);,
                       (*Vsync)[i].divide((*rho_ctime)[i],vbox,0,Yvel,1);,
                       (*Vsync)[i].divide((*rho_ctime)[i],vbox,0,Zvel,1););
            }
        }

        if (do_diffuse_sync)
        {
	    MultiFab** beta;
	    diffusion->allocFluxBoxesLevel(beta);
            if (is_diffusive[Xvel])
            {
                int rho_flag = (do_mom_diff == 0) ? 1 : 3;

                getViscosity(beta, cur_time);

                diffusion->diffuse_Vsync(Vsync,dt,be_cn_theta,Rh,rho_flag,beta);
            }
	    
	    if (!unity_Le 
		&& nspecies>0 
		&& do_add_nonunityLe_corr_to_rhoh_adv_flux 
		&& !do_mcdd
		&& !do_rk_diffusion)
	    {
		//
		// Diffuse the species syncs such that sum(SpecDiffSyncFluxes) = 0
		//
		differential_spec_diffuse_sync(dt);

                MultiFab Soln(grids,1,1);
                const Real cur_time  = state[State_Type].curTime();
                const Real a = 1.0;     // Passed around, but not used
                Real rhsscale;          //  -ditto-
                const int rho_flag = 2; // FIXME: Messy assumption
                MultiFab *alpha=0;      //  -ditto-
                MultiFab **fluxSC, **fluxNULN, **rhoh_visc;
                diffusion->allocFluxBoxesLevel(fluxSC,0,1);
                diffusion->allocFluxBoxesLevel(fluxNULN,0,nspecies);
                diffusion->allocFluxBoxesLevel(rhoh_visc,0,1);

                const int nGrow    = 1; // Size to grow fil-patched fab for T below
                const int dataComp = 0; // coeffs loaded into 0-comp for all species
                  
                getDiffusivity(rhoh_visc, cur_time, RhoH, 0, 1);

                for (int comp = 0; comp < nspecies; ++comp)
                {
                    const Real b     = be_cn_theta;
                    const int  sigma = first_spec + comp;
                    //
                    //  start by getting lambda/cp.Grad(Ysync)
                    //   (note: neg of usual diff flux)
                    //
                    ViscBndry      visc_bndry;
                    ABecLaplacian* visc_op;

                    visc_op = diffusion->getViscOp(sigma,a,b,cur_time,
                                                   visc_bndry,Rh,
                                                   rho_flag,&rhsscale,dataComp,
                                                   rhoh_visc,alpha);

                    visc_op->maxOrder(diffusion->maxOrder());

                    MultiFab::Copy(Soln,*Ssync,sigma-BL_SPACEDIM,0,1,0);

                    for (MFIter Smfi(Soln); Smfi.isValid(); ++Smfi)
                        Soln[Smfi].divide(S_new[Smfi],Smfi.validbox(),Density,0,1);

		    visc_op->compFlux(D_DECL(*fluxSC[0],*fluxSC[1],*fluxSC[2]),Soln);
                    for (int d = 0; d < BL_SPACEDIM; ++d)
                        fluxSC[d]->mult(-b/geom.CellSize()[d]);
                    //
                    // Here, get fluxNULN = (lambda/cp - rho.D)Grad(Ysync)
                    //                    = lambda/cp.Grad(Ysync) + SpecSyncDiffFlux
                    //
                    BL_ASSERT(spec_diffusion_flux_computed[comp]==HT_SyncDiffusion);

                    for (int d = 0; d < BL_SPACEDIM; ++d)
                    {
                        MFIter SDF_mfi(*SpecDiffusionFluxnp1[d]);

                        for ( ; SDF_mfi.isValid(); ++SDF_mfi)
                        {
                            FArrayBox& fluxSC_fab   = (*fluxSC[d])[SDF_mfi];
                            FArrayBox& fluxNULN_fab = (*fluxNULN[d])[SDF_mfi];
                            FArrayBox& SDF_fab = (*SpecDiffusionFluxnp1[d])[SDF_mfi];
                            const Box& ebox    = SDF_mfi.validbox();
                            fluxNULN_fab.copy(SDF_fab,ebox,comp,ebox,comp,1);
                            fluxNULN_fab.plus(fluxSC_fab,ebox,0,comp,1);
                        }
                    }
                    delete visc_op;
                }

                Soln.clear();
                diffusion->removeFluxBoxesLevel(fluxSC);
                diffusion->removeFluxBoxesLevel(rhoh_visc);
                //
                // Multiply fluxi by h_i (let FLXDIV routine below sum up the fluxes)
                //
                for (FillPatchIterator Tnew_fpi(*this,S_new,nGrow,cur_time,State_Type,Temp,1);
                     Tnew_fpi.isValid();
                     ++Tnew_fpi)
                {
                    const int i    = Tnew_fpi.index();
                    const Box& box = Tnew_fpi.validbox();

                    FArrayBox eTemp, h;
                    for (int d = 0; d < BL_SPACEDIM; ++d)
                    {
                        const Box ebox = BoxLib::surroundingNodes(box,d);
                        eTemp.resize(ebox,1);
                        FPLoc bc_lo =
                            fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).lo(d));
                        FPLoc bc_hi = 
                            fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).hi(d));
                        
                        center_to_edge_fancy(Tnew_fpi(),eTemp,BoxLib::grow(box,BoxLib::BASISV(d)),
                                             0,0,1,geom.Domain(),bc_lo,bc_hi);
                        
                        h.resize(ebox,nspecies);
                        getChemSolve().getHGivenT(h,eTemp,ebox,0,0);
                        (*fluxNULN[d])[i].mult(h,ebox,0,0,nspecies);
                    }
                }

                FArrayBox volume;

                for (MFIter Ssync_mfi(*Ssync); Ssync_mfi.isValid(); ++Ssync_mfi)
                {
                    const int i      = Ssync_mfi.index();
                    FArrayBox& syncn = (*Ssync)[i];
                    const FArrayBox& synco = (*Ssync)[i];
                    const Box& box = Ssync_mfi.validbox();

                    geom.GetVolume(volume,grids,i,GEOM_GROW);
                    //
                    // Multiply by dt*dt, one to make it extensive, and one because
                    // Ssync multiplied above by dt, need same units here.
                    //
                    const Real mult = dt*dt;
                    const int sigmaRhoH = RhoH - BL_SPACEDIM; // RhoH comp in Ssync
		    
                    FORT_INCRWEXTFLXDIV(box.loVect(), box.hiVect(),
                                        (*fluxNULN[0])[i].dataPtr(),
                                        ARLIM((*fluxNULN[0])[i].loVect()),
                                        ARLIM((*fluxNULN[0])[i].hiVect()),
                                        (*fluxNULN[1])[i].dataPtr(),
                                        ARLIM((*fluxNULN[1])[i].loVect()),
                                        ARLIM((*fluxNULN[1])[i].hiVect()),
#if BL_SPACEDIM == 3
                                        (*fluxNULN[2])[i].dataPtr(),
                                        ARLIM((*fluxNULN[2])[i].loVect()),
                                        ARLIM((*fluxNULN[2])[i].hiVect()),
#endif
                                        synco.dataPtr(sigmaRhoH),
                                        ARLIM(synco.loVect()),
                                        ARLIM(synco.hiVect()),
                                        syncn.dataPtr(sigmaRhoH),
                                        ARLIM(syncn.loVect()),
                                        ARLIM(syncn.hiVect()),
                                        volume.dataPtr(),
                                        ARLIM(volume.loVect()),
                                        ARLIM(volume.hiVect()),
                                        &nspecies, &mult);
                }

                diffusion->removeFluxBoxesLevel(fluxNULN);
            }
            else if (nspecies>0 && do_mcdd)
            {
                mcdd_diffuse_sync(dt);
            }

	    MultiFab **flux;
            diffusion->allocFluxBoxesLevel(flux);

            for (sigma = 0; sigma < numscal; sigma++)
            {
                int rho_flag = 0;
                int do_viscsyncflux = do_reflux;
		const int state_ind = BL_SPACEDIM + sigma;
		//
		// To diffuse, or not?
		// (1) Density, no
		// (2) RhoH...if diffusive
		// (3) Trac...if diffusive
		// (4) Spec:
		//    (a) if Le==1, and spec diffusive
		//    (b) if Le!=1, do differential diffusion instead, done above
		// (5) Temp, no (set instead by RhoH to Temp)
		const bool is_spec = state_ind<=last_spec && state_ind>=first_spec;
                int do_it
		    =  state_ind!=Density 
		    && state_ind!=Temp
		    && is_diffusive[state_ind]
		    && !(is_spec && !unity_Le)
		    && !(do_mcdd && (is_spec || state_ind==RhoH))
		    && !(do_rk_diffusion && (is_spec || state_ind==RhoH));
		
		if (do_it && (is_spec || state_ind==RhoH))
		    rho_flag = 2;

                if (do_it)
                {
                    MultiFab* alpha = 0;
                    getDiffusivity(beta, cur_time, state_ind, 0, 1);
                    
		    diffusion->diffuse_Ssync(Ssync,sigma,dt,be_cn_theta,Rh,
					     rho_flag,flux,0,beta,alpha);
		    if (do_viscsyncflux && level > 0)
		    {
			for (MFIter mfi(*Ssync); mfi.isValid(); ++mfi)
			{
			    const int i=mfi.index();
			    for (int d=0; d<BL_SPACEDIM; ++d)
                                getViscFluxReg().FineAdd((*flux[d])[i],d,i,
                                                         0,state_ind,1,dt);
			}
		    }
                }
            }
            diffusion->removeFluxBoxesLevel(flux);
	    diffusion->removeFluxBoxesLevel(beta);
        }
        //
        // For all conservative variables Q (other than density)
        // increment sync by (sync_for_rho)*q_presync.
        //
        for (MFIter mfi(*Ssync); mfi.isValid(); ++mfi)
        {
            const int i = mfi.index();

            int iconserved = -1;

            for (int istate = BL_SPACEDIM; istate < NUM_STATE; istate++)
            {
                if (istate != Density && advectionType[istate] == Conservative)
                {
                    iconserved++;

                    (*Ssync)[i].plus((*DeltaSsync)[i],grids[i],
                                     iconserved,istate-BL_SPACEDIM,1);
                }
            }
        }
        //
        // Increment the state (for all but rho, since that was done above)
        //
        for (MFIter mfi(S_new); mfi.isValid(); ++mfi)
        {
            const int i = mfi.index();

            for (int sigma = 0; sigma < numscal; sigma++)
            {
                if (!(BL_SPACEDIM+sigma == Density))
                {
                    S_new[i].plus((*Ssync)[i],grids[i],sigma,BL_SPACEDIM+sigma,1);
                }
            }
        }
        //
        // Recompute temperature and rho R T after the mac_sync.
        //
        RhoH_to_Temp(S_new);
        setThermoPress(cur_time);
        //
        // Get boundary conditions.
        //
        Real mult = 1.0;
        Array<int*>         sync_bc(grids.size());
        Array< Array<int> > sync_bc_array(grids.size());
        for (int i = 0; i < ngrids; i++)
        {
            sync_bc_array[i] = getBCArray(State_Type,i,Density,numscal);
            sync_bc[i]       = sync_bc_array[i].dataPtr();
        }
        //
        // Interpolate the sync correction to the finer levels.
        //
        IntVect ratio = IntVect::TheUnitVector();
        for (int lev = level+1; lev <= finest_level; lev++)
        {
            ratio                   *= parent->refRatio(lev-1);
            HeatTransfer& fine_level = getLevel(lev);
            MultiFab& S_new_lev      = fine_level.get_new_data(State_Type);
            //
            // New way of interpolating syncs to make sure mass is conserved
            // and to ensure freestream preservation for species & temperature.
            //
            const BoxArray& fine_grids = S_new_lev.boxArray();
            const int nghost           = S_new_lev.nGrow();
            MultiFab increment(fine_grids, numscal, nghost);
            increment.setVal(0,nghost);
            //
            // Note: we use the lincc_interp (which_interp==3) for density,
            // rho*h and rho*Y, cell_cons_interp for everything else. Doing
            // so is needed for freestream preservation of Y and T.  The setting
            // which_interp=5 calls the lincc_interp, but then follows with a
            // "protection" step to be sure that all the components but the
            // first and the last have their sync adjusted to try to preserve
            // positivity after the sync is applied.  The density sync is then
            // adjusted to be the sum of the species syncs.
            //
            // HACK note: Presently, the species mass syncs are redistributed 
            //            without consequence to the enthalpy sync.  Clearly
            //            the species carry enthalphy, so the enthalph sync should
            //            be adjusted as well.  Note yet sure how to do this correctly.
            //            Punt for now...
            //
            const SyncInterpType which_interp = CellConsProt_T;

            const int nComp = 2+nspecies;

            SyncInterp(*Ssync, level, increment, lev, ratio, 
                       Density-BL_SPACEDIM, Density-BL_SPACEDIM, nComp, 1, mult, 
                       sync_bc.dataPtr(), which_interp, Density);

            if (have_trac)
                SyncInterp(*Ssync, level, increment, lev, ratio, 
                           Trac-BL_SPACEDIM, Trac-BL_SPACEDIM, 1, 1, mult, 
                           sync_bc.dataPtr());

            if (have_rhort)
                SyncInterp(*Ssync, level, increment, lev, ratio, 
                           RhoRT-BL_SPACEDIM, RhoRT-BL_SPACEDIM, 1, 1, mult, 
                           sync_bc.dataPtr());

            SyncInterp(*Ssync, level, increment, lev, ratio, 
                       Temp-BL_SPACEDIM, Temp-BL_SPACEDIM, 1, 1, mult, 
                       sync_bc.dataPtr());

            if (do_set_rho_to_species_sum)
            {
                increment.setVal(0,Density-BL_SPACEDIM,1,0);

                for (int istate = first_spec; istate <= last_spec; istate++)
                { 
                    for (MFIter mfi(increment); mfi.isValid(); ++mfi)
                    {
                        int i = mfi.index();
                        increment[i].plus(increment[i],fine_grids[i],
					  istate-BL_SPACEDIM,Density-BL_SPACEDIM,1);
                    }
                }
            }

            for (MFIter mfi(increment); mfi.isValid(); ++mfi)
            {
                int i = mfi.index();
                S_new_lev[i].plus(increment[i],fine_grids[i],0,Density,numscal);
            }
            fine_level.make_rho_curr_time();
            fine_level.incrRhoAvg(increment,Density-BL_SPACEDIM,1.0);
            //
            // Recompute temperature and rho R T after interpolation of the mac_sync correction
            //   of the individual quantities rho, Y, T.
            //
            RhoH_to_Temp(S_new_lev);
            fine_level.setThermoPress(cur_time);
        }
        //
        // Average down Trac = rho R T after interpolation of the mac_sync correction
        //   of the individual quantities rho, Y, T.
        //
        for (int lev = finest_level-1; lev >= level; lev--)
        {
            HeatTransfer&   fine_lev = getLevel(lev+1);
            const BoxArray& fgrids   = fine_lev.grids;
            
            HeatTransfer&   crse_lev = getLevel(lev);
            const BoxArray& cgrids   = crse_lev.grids;
            const IntVect&  fratio   = crse_lev.fine_ratio;
            
            MultiFab& S_crse = crse_lev.get_new_data(State_Type);
            MultiFab& S_fine = fine_lev.get_new_data(State_Type);

            MultiFab fvolume, cvolume;

            crse_lev.geom.GetVolume(cvolume,crse_lev.grids,GEOM_GROW);
            fine_lev.geom.GetVolume(fvolume,fine_lev.grids,GEOM_GROW);

            const int pComp = (have_rhort ? RhoRT : Trac);
            crse_lev.NavierStokes::avgDown(cgrids,fgrids,
                                           S_crse,S_fine,
                                           cvolume,fvolume,
                                           lev,lev+1,pComp,1,fratio);
        }
    }

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;

    ParallelDescriptor::ReduceRealMax(run_time,IOProc);

    if (verbose && ParallelDescriptor::IOProcessor())
    {
        std::cout << "HeatTransfer:mac_sync(): lev: "
                  << level
                  << ", time: " << run_time << '\n';
    }

    sync_cleanup(DeltaSsync);
}

void
HeatTransfer::mcdd_diffuse_sync(Real dt)
{
    //
    // Compute the increment such that the composite equations are satisfied.
    // In the level advance, we solved
    //  (rho.Y)^* - (rho.Y)^n + theta.dt.L(Y)^* + (1-theta).dt.L(Y)^n = -dt.Div(rho.U.Y)
    // Here, we solve
    //  (rho.Y)^np1 - (rho.Y)^* + theta.dt(L(Y)^np1 - L(Y)^*) = -dt.Div(corr) = Ssync
    // where corr is the area.time weighted correction to add to the coarse to get fine
    //
    if (hack_nospecdiff)
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "... HACK!!! skipping mcdd sync diffusion " << '\n';

        for (int d=0; d<BL_SPACEDIM; ++d)
            SpecDiffusionFluxnp1[d]->setVal(0.0,0,nspecies);

        for (int comp=0; comp<nspecies; ++comp)
            spec_diffusion_flux_computed[comp] = HT_SyncDiffusion;

        return;            
    }

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "...doing mcdd sync diffusion..." << '\n';

    const Real time = state[State_Type].curTime();
    const int nGrow = 0;
    MultiFab Rhs(grids,nspecies+1,nGrow,Fab_allocate);
    const int dCompY = 0;
    const int dCompH = nspecies;
    PArray<MultiFab> fluxpre(BL_SPACEDIM,PArrayManage);
    for (int dir=0; dir<BL_SPACEDIM; ++dir)
        fluxpre.set(dir,new MultiFab(BoxArray(grids).surroundingNodes(dir),
                                     nspecies+1,0));
    
    // Rhs = Ssync + dt*theta*L(phi_presync) + (rhoY)_presync
    compute_mcdd_visc_terms(Rhs,dCompY,Rhs,dCompH,time,nGrow,DDOp::DD_RhoH,&fluxpre,dCompY,dCompH);

    const int spec_Ssync_sComp = first_spec - BL_SPACEDIM;
    const int rhoh_Ssync_sComp = RhoH - BL_SPACEDIM;
    MultiFab& S_new = get_new_data(State_Type);
    
    for (MFIter mfi(Rhs); mfi.isValid(); ++mfi)
    {
        FArrayBox& rhs = Rhs[mfi];
        const FArrayBox& snew = S_new[mfi];
        const FArrayBox& sync = (*Ssync)[mfi];
        const Box& box = mfi.validbox();

        rhs.mult(be_cn_theta*dt,box,0,nspecies+1);

        rhs.plus(sync,box,spec_Ssync_sComp,dCompY,nspecies);
        rhs.plus(sync,box,rhoh_Ssync_sComp,dCompH,1);

        rhs.plus(snew,box,first_spec,dCompY,nspecies);
        rhs.plus(snew,box,RhoH,dCompH,1);
    }

    // Save a copy of the pre-sync state
    MultiFab::Copy(*Ssync,S_new,first_spec,spec_Ssync_sComp,nspecies,0);
    MultiFab::Copy(*Ssync,S_new,RhoH,rhoh_Ssync_sComp,1,0);

    PArray<MultiFab> fluxpost(BL_SPACEDIM,PArrayManage);
    for (int dir=0; dir<BL_SPACEDIM; ++dir)
        fluxpost.set(dir,new MultiFab(BoxArray(grids).surroundingNodes(dir),
                                      nspecies+1,nGrow)); //stack H on Y's
    
    // Solve the system
    //mcdd_solve(Rhs,dCompY,dCompH,fluxpost,dCompY,dCompH,dt);
    BoxLib::Error("Fix the sync diffuse!");

    // Set Ssync to hold the resulting increment
    for (MFIter mfi(S_new); mfi.isValid(); ++mfi)
    {
        FArrayBox& sync = (*Ssync)[mfi];
        const FArrayBox& snew = S_new[mfi];
        const Box& box = mfi.validbox();
        
        sync.negate(box,spec_Ssync_sComp,nspecies);
        sync.negate(box,rhoh_Ssync_sComp,1);
        
        sync.plus(snew,box,first_spec,spec_Ssync_sComp,nspecies);
        sync.plus(snew,box,RhoH,rhoh_Ssync_sComp,1);
    }

    if (do_reflux)
    {
        {
            FArrayBox fluxinc;
            for (int d = 0; d < BL_SPACEDIM; d++)
            {
                for (MFIter mfi(fluxpost[d]); mfi.isValid(); ++mfi)
                {
                    const FArrayBox& fpost = fluxpost[d][mfi];
                    const FArrayBox& fpre = fluxpre[d][mfi];
                    const Box& ebox = fpost.box();

                    fluxinc.resize(ebox,nspecies+1);
                    fluxinc.copy(fpost,ebox,0,ebox,0,nspecies+1);
                    fluxinc.minus(fpre,ebox,0,0,nspecies+1);
                    fluxinc.mult(be_cn_theta);
                    if (level < parent->finestLevel())
                    {
                        //
                        // Note: The following inits do not trash each other
                        // since the component ranges don't overlap...
                        //
                        getLevel(level+1).getViscFluxReg().CrseInit(fluxinc,ebox,
                                                                    d,dCompY,first_spec,
                                                                    nspecies,-dt);
                        getLevel(level+1).getViscFluxReg().CrseInit(fluxinc,ebox,
                                                                    d,dCompH,RhoH,
                                                                    1,-dt);
                    }
                    if (level > 0)
                    {
                        getViscFluxReg().FineAdd(fluxinc,d,mfi.index(),
                                                 dCompY,first_spec,nspecies,dt);
                        getViscFluxReg().FineAdd(fluxinc,d,mfi.index(),
                                                 dCompH,RhoH,1,dt);
                    }
                }
            }
        }
        if (level < parent->finestLevel())
            getLevel(level+1).getViscFluxReg().CrseInitFinish();
    }
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "...finished differential sync diffusion..." << '\n';    
}

void
HeatTransfer::differential_spec_diffuse_sync(Real dt)
{
    if (hack_nospecdiff)
    {
        if (verbose && ParallelDescriptor::IOProcessor())
            std::cout << "... HACK!!! skipping spec sync diffusion " << '\n';

        for (int d=0; d<BL_SPACEDIM; ++d)
            SpecDiffusionFluxnp1[d]->setVal(0.0,0,nspecies);

        for (int comp=0; comp<nspecies; ++comp)
            spec_diffusion_flux_computed[comp] = HT_SyncDiffusion;

        return;            
    }

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "...doing differential sync diffusion..." << '\n';
    //
    // Do implicit c-n solve for each scalar...but dont reflux.
    // Save the fluxes, coeffs and source term, we need 'em for later
    // Actually, since Ssync multiplied by dt in mac_sync before
    // a call to this routine (I hope...), Ssync is in units of s,
    // not the "usual" ds/dt...lets convert it (divide by dt) so
    // we can use a generic flux adjustment function
    //
    const Real cur_time = state[State_Type].curTime();
    MultiFab **betanp1;
    diffusion->allocFluxBoxesLevel(betanp1,0,nspecies);
    getDiffusivity(betanp1, cur_time, first_spec, 0, nspecies);

    MultiFab Rhs(grids,nspecies,0);
    const int spec_Ssync_sComp = first_spec - BL_SPACEDIM;
    MultiFab::Copy(Rhs,*Ssync,spec_Ssync_sComp,0,nspecies,0);
    Rhs.mult(1.0/dt,0,nspecies,0); // Make Rhs in units of ds/dt again...
    //
    // Some standard settings
    //
    const Array<int> rho_flag(nspecies,2);
    const MultiFab* alpha = 0;
    MultiFab** fluxSC;
    diffusion->allocFluxBoxesLevel(fluxSC,0,1);

    const MultiFab* Rh = get_rho_half_time();

    for (int sigma = 0; sigma < nspecies; ++sigma)
    {
        //
        // Here, we use Ssync as a source in units of s, as expected by diffuse_Ssync
        // (i.e., ds/dt ~ d(Ssync)/dt, vs. ds/dt ~ Rhs in diffuse_scalar).  This was
        // apparently done to mimic diffuse_Vsync, which does the same, because the
        // diffused result is an acceleration, not a velocity, req'd by the projection.
        //
	const int ssync_ind = first_spec + sigma - Density;
	diffusion->diffuse_Ssync(Ssync,ssync_ind,dt,be_cn_theta,
				 Rh,rho_flag[sigma],fluxSC,
                                 sigma,betanp1,alpha);
	//
	// Pull fluxes into flux array
	//
	for (int d=0; d<BL_SPACEDIM; ++d)
	    MultiFab::Copy(*SpecDiffusionFluxnp1[d],*fluxSC[d],0,sigma,1,0);
	spec_diffusion_flux_computed[sigma] = HT_SyncDiffusion;
    }
    diffusion->removeFluxBoxesLevel(fluxSC);
    //
    // Modify update/fluxes to preserve flux sum = 0
    // (Be sure to pass the "normal" looking Rhs to this generic function)
    //
    const int sCompS = first_spec - BL_SPACEDIM;
    const MultiFab* old_sync = 0;
    const int dataComp = 0; 
    adjust_spec_diffusion_update(*Ssync,old_sync,sCompS,dt,cur_time,rho_flag,
                                 Rh,dataComp,&Rhs,alpha,betanp1);

    diffusion->removeFluxBoxesLevel(betanp1);

    Rhs.clear();
    //
    // Do refluxing AFTER flux adjustment
    //
    if (do_reflux)
    {
	for (int d=0; d<BL_SPACEDIM; ++d)
	{
	    for (MFIter fmfi(*SpecDiffusionFluxnp1[d]); fmfi.isValid(); ++fmfi)
	    {
                FArrayBox& fmfi_fab = (*SpecDiffusionFluxnp1[d])[fmfi];
		if (level < parent->finestLevel())
		    getLevel(level+1).getViscFluxReg().CrseInit(fmfi_fab,
								fmfi_fab.box(),
								d,0,first_spec,
								nspecies,-dt);
		if (level > 0)
		    getViscFluxReg().FineAdd(fmfi_fab,d,fmfi.index(),
					     0,first_spec,nspecies,dt);
	    }
	}
	if (level < parent->finestLevel())
	    getLevel(level+1).getViscFluxReg().CrseInitFinish();
    }
    
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "...finished differential sync diffusion..." << '\n';    
}

void
HeatTransfer::reflux ()
{
    if (level == parent->finestLevel())
        return;

    BL_ASSERT(do_reflux);

    MultiFab& S_crse = get_new_data(State_Type);
    //
    // First do refluxing step.
    //
    FluxRegister& fr_adv  = getAdvFluxReg(level+1);
    FluxRegister& fr_visc = getViscFluxReg(level+1);
    const Real    dt_crse = parent->dtLevel(level);
    const Real    scale   = 1.0/dt_crse;
    //
    // It is important, for do_mom_diff == 0, to do the viscous
    //   refluxing first, since this will be divided by rho_half
    //   before the advective refluxing is added.  In the case of
    //   do_mom_diff == 1, both components of the refluxing will
    //   be divided by rho^(n+1) in NavierStokes::level_sync.
    //
    MultiFab volume;

    geom.GetVolume(volume,grids,GEOM_GROW);

    fr_visc.Reflux(*Vsync,volume,scale,0,0,BL_SPACEDIM,geom);
    //
    // Set do_reflux_visc to 0 for debugging reasons only.
    //
    if (do_reflux_visc)
        fr_visc.Reflux(*Ssync,volume,scale,BL_SPACEDIM,0,NUM_STATE-BL_SPACEDIM,geom);

    const MultiFab* Rh = get_rho_half_time();

    if (do_mom_diff == 0) 
    {
        for (MFIter mfi(*Vsync); mfi.isValid(); ++mfi)
        {
            const int i = mfi.index();

            D_TERM((*Vsync)[i].divide((*Rh)[i],grids[i],0,Xvel,1);,
                   (*Vsync)[i].divide((*Rh)[i],grids[i],0,Yvel,1);,
                   (*Vsync)[i].divide((*Rh)[i],grids[i],0,Zvel,1););
        }
    }

    {
        FArrayBox tmp;
        for (MFIter mfi(*Ssync); mfi.isValid(); ++mfi)
        {
            const int i = mfi.index();

            tmp.resize(grids[i],1);
            tmp.copy((*Rh)[i],0,0,1);
            tmp.invert(1);

            for (int istate = BL_SPACEDIM; istate < NUM_STATE; istate++)
            {
                if (advectionType[istate] == NonConservative)
                {
                    const int sigma = istate -  BL_SPACEDIM;

                    (*Ssync)[i].mult(tmp,0,sigma,1);
                }
            }
        }
    }

    fr_adv.Reflux(*Vsync,volume,scale,0,0,BL_SPACEDIM,geom);
    fr_adv.Reflux(*Ssync,volume,scale,BL_SPACEDIM,0,NUM_STATE-BL_SPACEDIM,geom);

    const BoxArray& fine_boxes = getLevel(level+1).boxArray();
    const int nfine            = fine_boxes.size();
    //
    // This is necessary in order to zero out the contribution to any
    // coarse grid cells which underlie fine grid cells.
    //
    for (int kf = 0; kf < nfine; kf++)
    {
        Box bf = BoxLib::coarsen(fine_boxes[kf],fine_ratio);

        for (MFIter mfi(*Vsync); mfi.isValid(); ++mfi)
        {
            const int k  = mfi.index();
            const Box bx = bf & grids[k];

            if (bx.ok())
            {
                (*Vsync)[k].setVal(0,bx,0,BL_SPACEDIM);
                (*Ssync)[k].setVal(0,bx,0,NUM_STATE-BL_SPACEDIM);
            }
        }
    }
}

void
HeatTransfer::set_preferred_boundary_values (MultiFab& S,
                                             int       state_index,
                                             int       src_comp,
                                             int       dst_comp,
                                             int       num_comp,
                                             Real      time) const
{
    //
    // Only do copy if request contains affected states,
    // and fillpatched data known to be no good.
    //
    if (state_index == State_Type)
    {
        const TimeLevel whichTime = which_time(State_Type,time);
        //
        // To get chem-advanced data instead of FP'd data at old time
        //
        if (!FillPatchedOldState_ok && whichTime == AmrOldTime && src_comp > BL_SPACEDIM)
        {
            aux_boundary_data_old.copyTo(S,src_comp-BL_SPACEDIM,dst_comp,num_comp);
        }
        //
        // To get RhoH computed with current T, Y data instead of FP'd RhoH
        // Note: "advance" is to make sure that RhoH is correct as needed.
        //
        if (src_comp <= RhoH && src_comp + num_comp > RhoH)
        {
            const AuxBoundaryData& data = (whichTime == AmrOldTime) ? aux_boundary_data_old : aux_boundary_data_new;

            const int RhoHcomp = (whichTime == AmrOldTime) ? RhoH-BL_SPACEDIM : 1;

            data.copyTo(S,RhoHcomp,RhoH-src_comp+dst_comp,1);
        }
    }
}

void
HeatTransfer::calcViscosity (const Real time,
			     const Real dt,
			     const int  iteration,
			     const int  ncycle)
{
    const TimeLevel whichTime = which_time(State_Type, time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    compute_vel_visc(time, whichTime == AmrOldTime ? viscn_cc : viscnp1_cc);
}

void
HeatTransfer::calcDiffusivity (const Real time,
			       const Real dt,
			       const int  iteration,
			       const int  ncycle,
			       const int  src_comp,
			       const int  num_comp)
{
    calcDiffusivity(time,dt,iteration,ncycle,src_comp,num_comp,false);
}

void
HeatTransfer::calcDiffusivity (const Real time,
			       const Real dt,
			       const int  iteration,
			       const int  ncycle,
			       const int  src_comp,
			       const int  num_comp,
                               bool       do_VelVisc)
{
    if (do_mcdd || do_rk_diffusion) return;

    const TimeLevel whichTime = which_time(State_Type, time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    const int  nGrow           = 1;
    const int  offset          = BL_SPACEDIM + 1; // No diffusion coeff for vels or rho
    const int  last_comp       = src_comp + num_comp - 1;
    const bool has_spec        = src_comp < last_spec && last_comp > first_spec;
    const int  non_spec_comps  = std::max(0,first_spec-src_comp) + std::max(0,last_comp-last_spec);
    MultiFab&  visc            = (whichTime == AmrOldTime) ? (*diffn_cc) 
                                                           : (*diffnp1_cc);

    MultiFab temp(grids,1,nGrow), rhospec(grids,nspecies+1,nGrow);

    BL_ASSERT( !has_spec || (num_comp >= nspecies+non_spec_comps) );

    {
        FArrayBox tmp;

        for (FillPatchIterator Rho_and_spec_fpi(*this,rhospec,nGrow,time,State_Type,Density,nspecies+1),
                 Temp_fpi(*this,rhospec,nGrow,time,State_Type,Temp,1);
             Rho_and_spec_fpi.isValid() && Temp_fpi.isValid();
             ++Rho_and_spec_fpi, ++Temp_fpi)
        {
            const int idx = Rho_and_spec_fpi.index();
            //
            // Convert from RhoY_l to Y_l
            //
            tmp.resize(rhospec[idx].box(),1);
            tmp.copy(Rho_and_spec_fpi(),0,0,1);
            tmp.invert(1);

            for (int n = 1; n < nspecies+1; n++)
                Rho_and_spec_fpi().mult(tmp,0,n,1);

            rhospec[idx].copy(Rho_and_spec_fpi(),0,0,nspecies+1);

            temp[idx].copy(Temp_fpi(),0,0,1);
        }
    }

    BL_ASSERT(nspecies > 0 && has_spec);

    if (nspecies > 0 && has_spec)
    {
        BL_ASSERT(first_spec == Density+1);

        FArrayBox bcen;

        for (MFIter rho_and_spec(rhospec); rho_and_spec.isValid(); ++rho_and_spec)
        {
            const int  idx     = rho_and_spec.index();
            const Box& box     = rhospec[idx].box();
            const int  vflag   = do_VelVisc;
            FArrayBox& tempFab = temp[idx];

            BL_ASSERT(box == rhospec[idx].box());

            const int nc_bcen = nspecies+2;
            int       dotemp  = 1;
            bcen.resize(box,nc_bcen);

	    //CEG: computes rho*D_m and thermal conductivity
            FORT_SPECTEMPVISC(box.loVect(),box.hiVect(),
                              ARLIM(tempFab.loVect()),ARLIM(tempFab.hiVect()),
                              tempFab.dataPtr(),
                              ARLIM(rhospec[idx].loVect()),ARLIM(rhospec[idx].hiVect()),
                              rhospec[idx].dataPtr(1),
                              ARLIM(bcen.loVect()),ARLIM(bcen.hiVect()),bcen.dataPtr(),
                              &nc_bcen, &P1atm_MKS, &dotemp, &vflag);

            visc[idx].copy(bcen,0,first_spec-offset,nspecies);
            visc[idx].copy(bcen,nspecies,Temp-offset,1);

            if (do_VelVisc)
            {
                MultiFab& beta = (whichTime==AmrOldTime) ? (*viscn_cc) : (*viscnp1_cc);

                beta[idx].copy(bcen,nspecies+1,0,1);
            }
        }
    }
    //
    // Now get the rest.
    //
    for (int icomp = src_comp; icomp <= last_comp; icomp++)
    {
        const bool is_spec = icomp >= first_spec && icomp <= last_spec;

        if (!is_spec)
        {
            if (icomp == RhoH)
            {
                //
                // We assume that Temp has already been done.
                //
                BL_ASSERT(first_spec == Density+1);

                FArrayBox cpmix;

                for (MFIter rho_and_species(rhospec); rho_and_species.isValid(); ++rho_and_species)
                {
                    const int  idx = rho_and_species.index();
                    const Box& box = rhospec[idx].box();
		    // copy thermal diffusivity into enthalpy diffusivity
                    visc[idx].copy(visc[idx],Temp-offset,RhoH-offset,1);
                    cpmix.resize(box,1);
                    const int sCompT = 0, sCompY = 1, sCompCp = 0;
                    getChemSolve().getCpmixGivenTY(cpmix,temp[idx],rhospec[idx],box,sCompT,sCompY,sCompCp);
                    visc[idx].divide(cpmix,0,RhoH-offset,1);
                }
            }
            else if (icomp == Trac || icomp == RhoRT)
            {
                visc.setVal(trac_diff_coef, icomp-offset, 1, nGrow);
            }
        }
    }
}

void
HeatTransfer::getViscosity (MultiFab*  beta[BL_SPACEDIM],
                            const Real time)
{
    const TimeLevel whichTime = which_time(State_Type, time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    MultiFab* visc = (whichTime == AmrOldTime) ? viscn_cc : viscnp1_cc;

    for (MFIter viscMfi(*visc); viscMfi.isValid(); ++viscMfi)
    {
        const int i = viscMfi.index();

        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            FPLoc bc_lo = fpi_phys_loc(get_desc_lst()[State_Type].getBC(Density).lo(dir));
            FPLoc bc_hi = fpi_phys_loc(get_desc_lst()[State_Type].getBC(Density).hi(dir));

            center_to_edge_fancy((*visc)[viscMfi],(*beta[dir])[i],
                                 BoxLib::grow(grids[i],BoxLib::BASISV(dir)), 0, 0, 1,
                                 geom.Domain(), bc_lo, bc_hi);
        }
    }
}


void
HeatTransfer::getDiffusivity (MultiFab*  beta[BL_SPACEDIM],
                              const Real time,
                              const int  state_comp,
                              const int  dst_comp,
                              const int  ncomp)
{
    BL_ASSERT(state_comp > Density);

    const TimeLevel whichTime = which_time(State_Type, time);

    BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);

    MultiFab* diff      = (whichTime == AmrOldTime) ? diffn_cc : diffnp1_cc;
    const int offset    = BL_SPACEDIM + 1; // No diffusion coeff for vels or rho
    int       diff_comp = state_comp - offset;

   
    for (MFIter diffMfi(*diff); diffMfi.isValid(); ++diffMfi)
    {
        const int i = diffMfi.index();

        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            FPLoc bc_lo = fpi_phys_loc(get_desc_lst()[State_Type].getBC(Density).lo(dir));
            FPLoc bc_hi = fpi_phys_loc(get_desc_lst()[State_Type].getBC(Density).hi(dir));

            center_to_edge_fancy((*diff)[diffMfi],(*beta[dir])[i],
                                 BoxLib::grow(grids[i],BoxLib::BASISV(dir)), diff_comp, 
                                 dst_comp, ncomp, geom.Domain(), bc_lo, bc_hi);
        }
    }


    if (siegel_test && (state_comp == Temp || state_comp == RhoH))
        beta[1]->setVal(0);

    if (zeroBndryVisc > 0)
        zeroBoundaryVisc(beta,time,state_comp,dst_comp,ncomp);

}

void
HeatTransfer::zeroBoundaryVisc (MultiFab*  beta[BL_SPACEDIM],
                                const Real time,
                                const int  state_comp,
                                const int  dst_comp,
                                const int  ncomp) const
{
    BL_ASSERT(state_comp > Density);

    const int isrz = (int) geom.IsRZ();
    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        Box edom = BoxLib::surroundingNodes(geom.Domain(),dir);
        
        for (MFIter mfi(*(beta[dir])); mfi.isValid(); ++mfi)
        {
            FArrayBox& beta_fab = (*(beta[dir]))[mfi];
            const Box ebox      = BoxLib::surroundingNodes(mfi.validbox(),dir);
            FORT_ZEROVISC(beta_fab.dataPtr(dst_comp),
                          ARLIM(beta_fab.loVect()), ARLIM(beta_fab.hiVect()),
                          ebox.loVect(),  ebox.hiVect(),
                          edom.loVect(),  edom.hiVect(),
                          geom.CellSize(), geom.ProbLo(), phys_bc.vect(),
                          &dir, &isrz, &state_comp, &ncomp);
        }
    }
}

void
HeatTransfer::compute_vel_visc (Real      time,
                                MultiFab* beta)
{
    const int nGrow = beta->nGrow();

    BL_ASSERT(nGrow == 1);
    BL_ASSERT(first_spec == Density+1);

    MultiFab dummy(grids,1,0,Fab_noallocate);

    for (FillPatchIterator Temp_fpi(*this,dummy,nGrow,time,State_Type,Temp,1),
             Rho_and_spec_fpi(*this,dummy,nGrow,time,State_Type,Density,nspecies+1);
         Rho_and_spec_fpi.isValid() && Temp_fpi.isValid();
         ++Rho_and_spec_fpi, ++Temp_fpi)
    {
        const int  i            = Rho_and_spec_fpi.index();
        const Box& box          = Rho_and_spec_fpi().box();
        FArrayBox& temp         = Temp_fpi();
        FArrayBox& rho_and_spec = Rho_and_spec_fpi();
        //
        // Convert from RhoY_l to Y_l
        //
        FArrayBox tmp(box,1);
        tmp.copy(rho_and_spec,0,0,1);
        tmp.invert(1);

        for (int n = 1; n < nspecies+1; ++n)
            rho_and_spec.mult(tmp,0,n,1);

        FORT_VELVISC(box.loVect(),box.hiVect(),
                     ARLIM(temp.loVect()),ARLIM(temp.hiVect()),temp.dataPtr(),
                     ARLIM(rho_and_spec.loVect()),ARLIM(rho_and_spec.hiVect()),
                     rho_and_spec.dataPtr(1),
                     ARLIM(tmp.loVect()),ARLIM(tmp.hiVect()),tmp.dataPtr());

        (*beta)[i].copy(tmp,0,0,1);
    }
}

void
HeatTransfer::calc_divu (Real      time,
                         Real      dt,
                         MultiFab& divu)
{
    if(use_sdc){
      //
      // Get Mwmix, cpmix and pressure
      //
      const int nGrow = 0;

      int sCompR, sCompT, sCompY, sCompCp, sCompMw;
      sCompR=sCompT=sCompY=sCompCp=sCompMw=0;	

      MultiFab rho(grids,1,nGrow), temp(grids,1,nGrow),
	       ydot(grids,nspecies,nGrow);
      
      const MultiFab& Rho_time = get_rho(time);

      for (FillPatchIterator Temp_fpi(*this,temp,nGrow,time,State_Type,Temp,1);
	   Temp_fpi.isValid();
	   ++Temp_fpi)
	{
	  const int i = Temp_fpi.index();

	  rho[i].copy(Rho_time[i],0,sCompR,1);
	  temp[i].copy(Temp_fpi(),0,sCompT,1);
	}

      //	VisMF::Write(rho,"rho_orig");
      //	VisMF::Write(temp,"temp_orig");
      
      //
      // Note that state contains rho*species, so divide species by rho.
      //
      MultiFab species(grids,nspecies,nGrow);

      {
	FArrayBox tmp;

	for (FillPatchIterator Spec_fpi(*this,species,nGrow,time,State_Type,first_spec,nspecies);
	     Spec_fpi.isValid();
	     ++Spec_fpi)
	  {
	    const int i = Spec_fpi.index();
	    
	    species[i].copy(Spec_fpi(),0,sCompY,nspecies);

	    tmp.resize(grids[i],1);
	    tmp.copy(rho[i],sCompR,0,1);
	    tmp.invert(1);

	    for (int ispecies = 0; ispecies < nspecies; ispecies++)
	      species[i].mult(tmp,0,ispecies,1);
	  }
      }

      //	VisMF::Write(species,"species_orig");

      MultiFab mwmix(grids,1,nGrow), cp(grids,1,nGrow);
      
      for (MFIter Rho_mfi(rho); Rho_mfi.isValid(); ++Rho_mfi)
	{
	  const int  iGrid = Rho_mfi.index();
	  const Box& box   = Rho_mfi.validbox();
	  
	  BL_ASSERT(box == grids[iGrid]);
	  
	  getChemSolve().getMwmixGivenY(mwmix[iGrid],species[iGrid],
					box,sCompY,sCompMw);
	  getChemSolve().getCpmixGivenTY(cp[iGrid],temp[iGrid],species[iGrid],
					 box,sCompT,sCompY,sCompCp);
	}

      //	VisMF::Write(mwmix,"mwmix_orig");
      //	VisMF::Write(cp,"cp_orig");


      // Compute
      //  div u = (div lambda grad T + 
      //           sum_l rho D grad h_l dot grad Y_l)/(c_p*T*rho)
      //
      MultiFab visc_terms(grids,1,1);
      getViscTerms(visc_terms,Temp,1,time);
      MultiFab::Copy(divu,visc_terms,0,0,1,nGrow);
      for (MFIter Divu_mfi(divu); Divu_mfi.isValid(); ++Divu_mfi)
	{
	  const int iGrid = Divu_mfi.index();
	  divu[iGrid].divide(rho[iGrid],grids[iGrid],sCompR,0,1);
	  divu[iGrid].divide(temp[iGrid],grids[iGrid],sCompT,0,1);
	}
      //	VisMF::Write(divu,"divu_visc_orig");


      // Compute
      //  divu = divu + W * (del . rho D_l grad Y_l)/(rho*W_l)
      //
      MultiFab delta_divu(grids,1,nGrow), spec_visc_terms(grids,nspecies,0);

      delta_divu.setVal(0.0);
      
      const Array<Real> mwt = getChemSolve().speciesMolecWt();
      
      getViscTerms(spec_visc_terms,first_spec,nspecies,time);
      //	VisMF::Write(spec_visc_terms,"spec_visc_orig");

      for (MFIter mfi(spec_visc_terms); mfi.isValid(); ++mfi)
	{
	  const int iGrid = mfi.index();
	  
	  for (int comp = 0; comp < nspecies; ++comp)
	    {
	      spec_visc_terms[mfi].mult(1.0/mwt[comp],comp,1);
	      delta_divu[mfi].plus(spec_visc_terms[mfi],grids[iGrid],comp,0,1);
	    }
	}

      spec_visc_terms.clear();
      
      for (MFIter Divu_mfi(divu); Divu_mfi.isValid(); ++Divu_mfi)
	{
	  const int  iGrid = Divu_mfi.index();
	  const Box& box   = Divu_mfi.validbox();
	  delta_divu[iGrid].divide(rho[iGrid],box,sCompR,0,1);
	  delta_divu[iGrid].mult(mwmix[iGrid],box,0,0,1);
	  divu[iGrid].plus(delta_divu[iGrid],box,0,0,1);
	}

      rho.clear();
      
      // Compute
      //  divu = divu + (h_l/(c_p*T) - W/W_l) * omega_l^dot/rho
      //
      if (dt > 0.0)
	{
	  if (hack_nochem){
	    species.clear();
	    return;
	  }

	  FArrayBox h;

	  const int sCompH = 0;

	  // CEG:: is this getting P at the right time?
	  Real p_amb, dpdt_factor;
	  FORT_GETPAMB(&p_amb, &dpdt_factor);
	  const Real Patm = p_amb / P1atm_MKS;

	  // CEG::Is this okay in place of a FPI???
	  for (MFIter Ydot_mfi(ydot); Ydot_mfi.isValid(); ++Ydot_mfi)
	    {
	      const int i = Ydot_mfi.index();

	      h.resize(BoxLib::grow(grids[i],nGrow),nspecies);

	      getChemSolve().getHGivenT(h,temp[i],grids[i],sCompT,sCompH);
	      
	      //Compute
	      //  -omega-dot/rho = -Y_l-dot
	      //
	      // CEG:: this does the job, but it also does extra work
	      //  could i use CKYTCR instead of CKYTCP (use rho instead of
	      //  Press and Temp)
	      // is Patm the right thing????
	      getChemSolve().reactionRateY(ydot[i],species[i],temp[i],Patm,
					   grids[i],sCompY,sCompT,0);

	      for (int ispec = 0; ispec <= nspecies; ispec++)
		{

		  delta_divu[i].copy(h,ispec,0,1);
		  delta_divu[i].divide(cp[i]);
		  delta_divu[i].divide(temp[i]);
		  delta_divu[i].mult(ydot[i],ispec,0,1);
		  divu[i].minus(delta_divu[i]);

		  delta_divu[i].copy(mwmix[i],0,0,1);
		  delta_divu[i].divide(mwt[ispec]);
		  delta_divu[i].mult(ydot[i],ispec,0,1);
		  divu[i].plus(delta_divu[i]);
		}
	    }
	}

      species.clear();
    }
  
    else {
      if (do_rk_diffusion)
	{
	  //
	  // Calculate right side of constraint: Runge-Kutta operator.
	  //

	  // check dt
	  BL_ASSERT(dt > 0);

	  // no growth cells
	  const int nGrow = 0;
	
	  // apply the diffusion operator to the old or new state.  only the 
	  // divergences are used here, but space is still needed for the others.
	  MultiFab* div_of_flux_for_H;
	  MultiFab* div_of_flux_for_Y;
	  MultiFab** flux_for_H;
	  MultiFab** flux_for_Y;

	  // the divergences should not be scaled, so pass 1 as the scaling argument
	  rk_diffusion_operator (time,
				 1.0,
				 div_of_flux_for_H,
				 div_of_flux_for_Y,
				 flux_for_H,
				 flux_for_Y);

	  // get rho and T
	  MultiFab rho(grids,1,nGrow);
	  MultiFab T(grids,1,nGrow);
	
	  const MultiFab& Rho_time = get_rho(time);
	  MFIter          Rho_mfi(Rho_time);
	
	  for (FillPatchIterator T_fpi(*this,T,nGrow,time,State_Type,Temp,1);
	       Rho_mfi.isValid() && T_fpi.isValid();
	       ++Rho_mfi, ++T_fpi)
	    {
	      const int i = Rho_mfi.index();
	    
	      rho[i].copy(Rho_time[i],0,0,1);
	      T[i].copy(T_fpi(),0,0,1);
	    }

	  // get Y, which is needed only to get cp, h_k, and mmw
	  MultiFab Y(grids,nspecies,nGrow);

	  {
            FArrayBox tmp;
            for (FillPatchIterator Y_fpi(*this,Y,nGrow,time,State_Type,first_spec,nspecies);
                 Y_fpi.isValid();
                 ++Y_fpi)
	      {
                const int i = Y_fpi.index();
	    
                Y[i].copy(Y_fpi(),0,0,nspecies);
	    
                tmp.resize(grids[i],1);
                tmp.copy(rho[i],0,0,1);
                tmp.invert(1);
	    
                for (int ispecies = 0; ispecies < nspecies; ispecies++)
		  Y[i].mult(tmp,0,ispecies,1);
	      }
	  }
	
	  // get cp, h_k, and mmw
	  MultiFab cp(grids,1,nGrow);
	  MultiFab h(grids,nspecies,nGrow);
	  MultiFab mmw(grids,1,nGrow);
	
	  for (MFIter Rho_mfi(rho); Rho_mfi.isValid(); ++Rho_mfi)
	    {
	      const int  iGrid = Rho_mfi.index();
	      const Box& box   = Rho_mfi.validbox();
	    
	      BL_ASSERT(box == grids[iGrid]);
	    
	      getChemSolve().getMwmixGivenY(mmw[iGrid],Y[iGrid],box,0,0);
	      getChemSolve().getCpmixGivenTY(cp[iGrid],T[iGrid],Y[iGrid],box,0,0,0);
	      getChemSolve().getHGivenT(h[iGrid],T[iGrid],box,0,0);
	    }

	  // form in divu the first term: - div_of_flux_for_H / (cp rho T)

	  // note the arguments for MultiFab:: procedures are 
	  // (dst, src, srccomp, dstcomp, ncomp, nghost);
	  MultiFab::Copy (divu, *div_of_flux_for_H, 0, 0, 1, nGrow);
	  for (MFIter divu_mfi(divu); divu_mfi.isValid(); ++divu_mfi)
	    {
	      const int iGrid = divu_mfi.index();
	      divu[iGrid].mult(-1.0);
	      divu[iGrid].divide(cp[iGrid],grids[iGrid],0,0,1);
	      divu[iGrid].divide(rho[iGrid],grids[iGrid],0,0,1);
	      divu[iGrid].divide(T[iGrid],grids[iGrid],0,0,1);
	    }

	  // add to divu the products of the other terms

	  const Array<Real> mw = getChemSolve().speciesMolecWt();
	  MultiFab term1A(grids,1,nGrow);
	  MultiFab term1B(grids,1,nGrow);
	  MultiFab term2A(grids,1,nGrow);
	  MultiFab term2B(grids,1,nGrow);

	  // note Ydot_Type appears to have 1 growth cell, but the following
	  // iterator discards that, so the following arithmetic can be done 
	  // without using boxes to restrict to the valid region
	  for (FillPatchIterator Ydot_fpi(*this,divu,0,time,Ydot_Type,0,nspecies);
	       Ydot_fpi.isValid();
	       ++Ydot_fpi)
	    {
	      const int iGrid = Ydot_fpi.index();
	      for (int n = 0; n < nspecies; ++n)
		{
		  // term1A = h_n / (cp T)
		  term1A[iGrid].copy(h[iGrid],n,0,1);
		  term1A[iGrid].divide(cp[iGrid],0,0,1);
		  term1A[iGrid].divide(T[iGrid],0,0,1);
		  // term1B = mmw / mw_n
		  term1B[iGrid].copy(mmw[iGrid],0,0,1);
		  term1B[iGrid].mult(1.0/mw[n]);
		  // term1A = term1A - term1B
		  term1A[iGrid].minus(term1B[iGrid],0,0,1);

		  // term2A = div_of_flux_for_Y_n / rho
		  term2A[iGrid].copy((*div_of_flux_for_Y)[iGrid],n,0,1);
		  term2A[iGrid].divide(rho[iGrid],0,0,1);
		  // term2B = Ydot_n 
		  // JFG: should we divide ydot by rho?
		  // Ydot apparently is - omega, that is,
		  // it has a minus sign built in, so we
		  // only want an addition here.
		  term2B[iGrid].copy(Ydot_fpi(),n,0,1);
		  // term2B[iGrid].divide(rho[iGrid],0,0,1); // test to divide Ydot by rho
		  // term2A = term2A - omega
		  // term2A = term2A + term2B
		  term2A[iGrid].plus(term2B[iGrid],0,0,1);

		  // term1A = term1A * term2A
		  term1A[iGrid].mult(term2A[iGrid],0,0,1);
		  // divu = divu + term1A
		  divu[iGrid].plus(term1A[iGrid],0,0,1);
		}
	    }

	  // delete the space for fluxes and updates
	  delete div_of_flux_for_H;
	  delete div_of_flux_for_Y;
	  diffusion->removeFluxBoxesLevel (flux_for_H);
	  diffusion->removeFluxBoxesLevel (flux_for_Y);
	}
      else
	{
	  //
	  // Get Mwmix, cpmix and pressure
	  //
	  const int nGrow = 0;

	  int sCompR, sCompT, sCompY, sCompCp, sCompMw;
	  sCompR=sCompT=sCompY=sCompCp=sCompMw=0;	
	  //
	  // mcdd: get Y,T visc terms together, use in place of individual calls below
	  //
	  MultiFab mcViscTerms;
	  const int vtCompT = nspecies; // T terms stacked on Y terms
	  const int vtCompY = 0;
	  if (do_mcdd)
	    {
	      mcViscTerms.define(grids,nspecies+1,nGrow,Fab_allocate);
	      compute_mcdd_visc_terms(mcViscTerms,vtCompY,mcViscTerms,vtCompT,time,nGrow,DDOp::DD_Temp);
	    }

	  MultiFab rho(grids,1,nGrow), temp(grids,1,nGrow);

	  const MultiFab& Rho_time = get_rho(time);

	  for (FillPatchIterator Temp_fpi(*this,temp,nGrow,time,State_Type,Temp,1);
	       Temp_fpi.isValid();
	       ++Temp_fpi)
	    {
	      const int i = Temp_fpi.index();

	      rho[i].copy(Rho_time[i],0,sCompR,1);
	      temp[i].copy(Temp_fpi(),0,sCompT,1);
	    }

	  //	VisMF::Write(rho,"rho_orig");
	  //	VisMF::Write(temp,"temp_orig");

	  //
	  // Note that state contains rho*species, so divide species by rho.
	  //
	  MultiFab species(grids,nspecies,nGrow);

	  {
            FArrayBox tmp;

            for (FillPatchIterator Spec_fpi(*this,species,nGrow,time,State_Type,first_spec,nspecies);
                 Spec_fpi.isValid();
                 ++Spec_fpi)
	      {
                const int i = Spec_fpi.index();

                species[i].copy(Spec_fpi(),0,sCompY,nspecies);

                tmp.resize(grids[i],1);
                tmp.copy(rho[i],sCompR,0,1);
                tmp.invert(1);

                for (int ispecies = 0; ispecies < nspecies; ispecies++)
		  species[i].mult(tmp,0,ispecies,1);
	      }
	  }

	  //	VisMF::Write(species,"species_orig");

	  MultiFab mwmix(grids,1,nGrow), cp(grids,1,nGrow);

	  for (MFIter Rho_mfi(rho); Rho_mfi.isValid(); ++Rho_mfi)
	    {
	      const int  iGrid = Rho_mfi.index();
	      const Box& box   = Rho_mfi.validbox();

	      BL_ASSERT(box == grids[iGrid]);

	      getChemSolve().getMwmixGivenY(mwmix[iGrid],species[iGrid],
					    box,sCompY,sCompMw);
	      getChemSolve().getCpmixGivenTY(cp[iGrid],temp[iGrid],species[iGrid],
					     box,sCompT,sCompY,sCompCp);
	    }

	  //	VisMF::Write(mwmix,"mwmix_orig");
	  //	VisMF::Write(cp,"cp_orig");

	  species.clear();
	  //
	  // divu = 1/T DT/dt + W_mix * sum (1/W)DY/Dt
	  //
	  // Compute rho*DT/Dt as
	  //
	  //   1/c_p (div lambda grad T + sum_l rho D grad h_l dot grad Y_l)
	  //
	  if (do_mcdd)
	    {
	      MultiFab::Copy(divu,mcViscTerms,vtCompT,0,1,nGrow);
	    }
	  else
	    {
	      MultiFab visc_terms(grids,1,1);
	      getViscTerms(visc_terms,Temp,1,time);
	      MultiFab::Copy(divu,visc_terms,0,0,1,nGrow);
	    }

	  //	VisMF::Write(divu,"divu_visc_orig");

	  for (MFIter Divu_mfi(divu); Divu_mfi.isValid(); ++Divu_mfi)
	    {
	      const int iGrid = Divu_mfi.index();
	      divu[iGrid].divide(rho[iGrid],grids[iGrid],sCompR,0,1);
	      divu[iGrid].divide(temp[iGrid],grids[iGrid],sCompT,0,1);
	    }

	  MultiFab delta_divu(grids,1,nGrow), spec_visc_terms(grids,nspecies,0);

	  delta_divu.setVal(0.0);

	  const Array<Real> mwt = getChemSolve().speciesMolecWt();

	  if (do_mcdd)
	    {
	      MultiFab::Copy(spec_visc_terms,mcViscTerms,vtCompY,0,nspecies,0);
	    }
	  else
	    {
	      getViscTerms(spec_visc_terms,first_spec,nspecies,time);
	    }

	  //	VisMF::Write(spec_visc_terms,"spec_visc_orig");

	  for (MFIter mfi(spec_visc_terms); mfi.isValid(); ++mfi)
	    {
	      const int iGrid = mfi.index();

	      for (int comp = 0; comp < nspecies; ++comp)
		{
		  spec_visc_terms[mfi].mult(1.0/mwt[comp],comp,1);
		  delta_divu[mfi].plus(spec_visc_terms[mfi],grids[iGrid],comp,0,1);
		}
	    }

	  spec_visc_terms.clear();

	  for (MFIter Divu_mfi(divu); Divu_mfi.isValid(); ++Divu_mfi)
	    {
	      const int  iGrid = Divu_mfi.index();
	      const Box& box   = Divu_mfi.validbox();
	      delta_divu[iGrid].divide(rho[iGrid],box,sCompR,0,1);
	      delta_divu[iGrid].mult(mwmix[iGrid],box,0,0,1);
	      divu[iGrid].plus(delta_divu[iGrid],box,0,0,1);
	    }

	  rho.clear();

	  if (dt > 0.0)
	    {
	      //
	      // Increment divu by
	      //    sum_l (h_l/(c_p*T) - mw_mix/mw_l)*delta Y_l/dt 
	      // (i.e., Y_l"dot")
	      //
	      FArrayBox h;

	      const int sCompH = 0;

	      for (FillPatchIterator Ydot_fpi(*this,delta_divu,0,time,Ydot_Type,0,nspecies);
		   Ydot_fpi.isValid();
		   ++Ydot_fpi)
		{
		  const int i = Ydot_fpi.index();

		  h.resize(BoxLib::grow(grids[i],nGrow),nspecies);

		  getChemSolve().getHGivenT(h,temp[i],grids[i],sCompT,sCompH);

		  for (int istate = first_spec; istate <= last_spec; istate++)
		    {
		      const int ispec = istate-first_spec;

		      delta_divu[i].copy(h,ispec,0,1);
		      delta_divu[i].divide(cp[i]);
		      delta_divu[i].divide(temp[i]);
		      delta_divu[i].mult(Ydot_fpi(),ispec,0,1);
		      divu[i].plus(delta_divu[i]);

		      delta_divu[i].copy(mwmix[i],0,0,1);
		      delta_divu[i].divide(mwt[ispec]);
		      delta_divu[i].mult(Ydot_fpi(),ispec,0,1);
		      divu[i].minus(delta_divu[i]);
		    }
		}
	    }
	}
    }
}

//
// Compute the Eulerian Dp/Dt for use in pressure relaxation.
//
void
HeatTransfer::calc_dpdt (Real      time,
                         Real      dt_,
                         MultiFab& dpdt,
                         MultiFab* u_mac)
{
    Real dt = crse_dt;

    Real p_amb, dpdt_factor;
    
    FORT_GETPAMB(&p_amb, &dpdt_factor);
    
    if (dt <= 0.0 || dpdt_factor <= 0)
    {
        dpdt.setVal(0);
        return;
    }
    
    MultiFab& S_old = get_old_data(State_Type);
    
    const int pComp = (have_rhort ? RhoRT : Trac);
    const int nGrow = 1;
    
    int sCompR, sCompT, sCompY, sCompCp, sCompMw;
    sCompR=sCompT=sCompY=sCompCp=sCompMw=0;
    
    MultiFab temp(grids,1,nGrow);
    MultiFab rhoRT(grids,1,nGrow);
    
    const MultiFab& rho = get_rho(time);

    BL_ASSERT(rho.boxArray()  == S_old.boxArray());
    BL_ASSERT(temp.boxArray() == S_old.boxArray());
    BL_ASSERT(dpdt.boxArray() == S_old.boxArray());
    
    for (FillPatchIterator Temp_fpi(*this,temp,nGrow,time,State_Type,Temp,1);
         Temp_fpi.isValid();
         ++Temp_fpi)
    {
        temp[Temp_fpi.index()].copy(Temp_fpi(),0,sCompT,1);
    }

    for (FillPatchIterator rhoRT_fpi(*this,rhoRT,nGrow,time,State_Type,pComp,1);
         rhoRT_fpi.isValid();
         ++rhoRT_fpi)
    {
        rhoRT[rhoRT_fpi.index()].copy(rhoRT_fpi(),0,0,1);
    }
    //
    // Note that state contains rho*species, so divide species by rho.
    //
    MultiFab species(grids,nspecies,nGrow);

    {
        FArrayBox tmp;

        for (FillPatchIterator Spec_fpi(*this,species,nGrow,time,State_Type,first_spec,nspecies);
             Spec_fpi.isValid();
             ++Spec_fpi)
        {
            const int i  = Spec_fpi.index();
            const Box bx = BoxLib::grow(grids[i],nGrow);

            species[i].copy(Spec_fpi(),0,sCompY,nspecies);

            tmp.resize(bx,1);
            tmp.copy(rho[i],sCompR,0,1);
            tmp.invert(1);

            for (int ispecies = 0; ispecies < nspecies; ispecies++)
                species[i].mult(tmp,0,ispecies,1);
        }
    }
    
    MultiFab mwmix(grids,1,nGrow);
    MultiFab cp(grids,1,nGrow);
    
    for (MFIter Rho_mfi(rho); Rho_mfi.isValid(); ++Rho_mfi)
    {
        const int idx = Rho_mfi.index();
        const Box box = BoxLib::grow(Rho_mfi.validbox(),nGrow);
        
        BL_ASSERT(Rho_mfi.validbox() == grids[idx]);
        
        getChemSolve().getMwmixGivenY(mwmix[idx],species[idx],box,sCompY,sCompMw);
        getChemSolve().getCpmixGivenTY(cp[idx],temp[idx],species[idx],box,sCompT,sCompY,sCompCp);
    }

    temp.clear();
    species.clear();
    //
    // Now do pressure relaxation.
    //
    const Real* dx = geom.CellSize();
    //
    //  Get gamma = c_p/(c_p-R)
    //
    MultiFab gamma(grids,1,nGrow);

    {
        FArrayBox rmix;
        for (MFIter mfi(gamma); mfi.isValid(); ++mfi)
        {
            const int idx = mfi.index();
            gamma[idx].copy(cp[idx]);
            rmix.resize(BoxLib::grow(grids[idx],nGrow),1);
            rmix.setVal(rgas);
            rmix.divide(mwmix[idx]);
            gamma[idx].minus(rmix);
            gamma[idx].divide(cp[idx]);
            gamma[idx].invert(1.0);
        }
    }

    cp.clear();
    mwmix.clear();

    FArrayBox ugradp;
    
    for (MFIter mfi(dpdt); mfi.isValid(); ++mfi)
    {
        const int i = mfi.index();
        
        dpdt[i].copy(S_old[i],grids[i],pComp,grids[i],0,1);
        dpdt[i].plus(-p_amb);
        dpdt[i].mult(1.0/dt);

        ugradp.resize(grids[i],1);
        
        const int* lo = grids[i].loVect();
        const int* hi = grids[i].hiVect();

        FORT_COMPUTE_UGRADP(rhoRT[i].dataPtr(0), 
                            ARLIM(rhoRT[i].box().loVect()), 
                            ARLIM(rhoRT[i].box().hiVect()),
                            ugradp.dataPtr(), 
                            ARLIM(ugradp.box().loVect()), 
                            ARLIM(ugradp.box().hiVect()),
                            u_mac[0][i].dataPtr(),
                            ARLIM(u_mac[0][i].box().loVect()),
                            ARLIM(u_mac[0][i].box().hiVect()),
                            u_mac[1][i].dataPtr(),
                            ARLIM(u_mac[1][i].box().loVect()),
                            ARLIM(u_mac[1][i].box().hiVect()),
#if (BL_SPACEDIM == 3)
                            u_mac[2][i].dataPtr(),
                            ARLIM(u_mac[2][i].box().loVect()),
                            ARLIM(u_mac[2][i].box().hiVect()),
#endif
                            lo,hi,dx);
        //
        // Note that this is minus because the term we want to add to S is
        //  - Dp/Dt.   We already have (p-p0)/dt = -dp/dt, so now
        // we subtract ugradp to form -Dp/dt.  
        // (Note the dp/dt term is negative because
        //  p is the current value, p0 is the value we're trying to get to,
        //  so dp/dt = (p0 - p)/dt.)
        //
        dpdt[i].minus(ugradp,0,0,1);
        //
        // Make sure to divide by gamma *after* subtracting ugradp.
        //
        dpdt[i].divide(gamma[i]);

        if (dpdt_option == 0)
        {
            dpdt[i].divide(S_old[i],pComp,0,1);
        }
        else if (dpdt_option == 1)
        {
            dpdt[i].divide(p_amb);
        }
        else
        {
            const int ncomp = 1;
            FArrayBox p_denom(grids[i],ncomp);
            p_denom.copy(S_old[i],grids[i],pComp,grids[i],0,ncomp);
            Real num_norm = dpdt[i].norm(0,0,1);
            FabMinMax(p_denom,grids[i],2*num_norm*Real_MIN,p_amb,0,ncomp);
            dpdt[i].divide(p_denom);
        }
        
        dpdt[i].mult(dpdt_factor);
    }
}

//
// Function to use if Divu_Type and Dsdt_Type are in the state.
//

void
HeatTransfer::calc_dsdt (Real      time,
                         Real      dt,
                         MultiFab& dsdt)
{
    MultiFab& Divu_new = get_new_data(Divu_Type);
    MultiFab& Divu_old = get_old_data(Divu_Type);

    dsdt.setVal(0);

    for (MFIter mfi(dsdt); mfi.isValid(); ++mfi)
    {
        const int k = mfi.index();
        dsdt[k].copy(Divu_new[k],grids[k]);
        dsdt[k].minus(Divu_old[k],grids[k],0,0,1);
        dsdt[k].divide(dt,grids[k],0,1);
    }
}

void
HeatTransfer::RhoH_to_Temp (MultiFab& S,
                            int       nGrow)

{
    //
    // Solve hmix = sum(hl(temp).Yl) for the temp field in S.  S is state-like.
    //
    // Be careful -- this is called for levels other than the current level.
    //
    // nGrow is number of grow cells wanted, assumes the rest of the state
    // trusted out there...must make temp same size as S.
    //
    const BoxArray& sgrids = S.boxArray();

    MultiFab temp(sgrids,1,S.nGrow());

    const int do_minmax = 1;

    RhoH_to_Temp(S,temp,nGrow,do_minmax);

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        const int k   = mfi.index();
        Box       box = BoxLib::grow(sgrids[k],nGrow);
        S[k].copy(temp[k],box,0,box,Temp,1);
    }
}

//
// Taking an initial guess from S, solve hmix = sum(hl(temp).Yl) for temp.
// hmix and Yl are taken from S (and are assumed to be multiplied by rho
// (i.e. S holds rho.hmix and rho.Yl).
//
// Be careful -- this is called for levels other than the current level
// Note: we only do this on the valid region. The ghost cells
//       are "operated" on, but only in copying S to temp
//       and applying min/max to temp; no actual rhoh-to-temp conversion
//       is applied to the ghost cells...superceeded, see below
// Note: assumes that Temp slot of S contains a reasonable initial guess
//       for temp
// Note: S is unchanged by this function, but we use it for temporary
//       space, in particular, to hold primitives h and Y_l
// Note: nGrow is number of grow cells wanted, assumes the rest of the state
//       trusted out there, nGrow must be same in temp and S
// Note: no good reason for above restriction on nGrow, removed
//
// JFG: RhoH_to_Temp is the top of a rather long sequence of wrappers:
//
//         RhoH_to_Temp    in   HeatTransfer.cpp  calls
//         getTGivenHY     in     ChemDriver.cpp  calls
//         FORT_TfromHY    in  ChemDriver_?D.F    calls
//         FORT_TfromHYpt  in   ChemDriver_F.F    does the work
//

void
HeatTransfer::RhoH_to_Temp (MultiFab& S,
                            MultiFab& temp,
                            int       nGrow,
                            int       dominmax)
{
    BL_ASSERT(S.nGrow() >= nGrow  &&  temp.nGrow() >= nGrow);

    const BoxArray& sgrids = S.boxArray();

    const int sCompT    = 0;
    int       max_iters = 0;

    MultiFab::Copy(temp,S,Temp,sCompT,1,nGrow);

    FArrayBox tmp;

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
        const int k   = mfi.index();
        Box       box = BoxLib::grow(sgrids[k],nGrow);
        //
        // Convert rho*h to h and rho*Y to Y for this operation.
        //
        tmp.resize(box,1);
        tmp.copy(S[k],Density,0,1);
        tmp.invert(1);

        S[k].mult(tmp,0,RhoH,1);

        for (int spec = first_spec; spec <= last_spec; spec++)
            S[k].mult(tmp,0,spec,1);

        int iters = getChemSolve().getTGivenHY(temp[k],S[k],S[k],
                                               box,RhoH,first_spec,sCompT);
        if (iters < 0)
            BoxLib::Error("HeatTransfer::RhoH_to_Temp: error in H->T");

        max_iters = std::max(max_iters,iters);

        if (dominmax)
            FabMinMax(S[k], box, htt_tempmin, htt_tempmax, Temp, 1);
        //
        // Convert back to rho*h and rho*Y
        //
        S[k].mult(S[k],box,Density,RhoH,1);
        for (int spec = first_spec; spec <= last_spec; spec++)
            S[k].mult(S[k],box,Density,spec,1);
    }

    if (verbose)
    {
        const int IOProc = ParallelDescriptor::IOProcessorNumber();

        ParallelDescriptor::ReduceIntMax(max_iters,IOProc);

        if (verbose && ParallelDescriptor::IOProcessor())
        {
            std::cout << "HeatTransfer::RhoH_to_Temp: max_iters = " << max_iters << '\n';
        }
    }
}

void
HeatTransfer::compute_cp_and_hmix (const MultiFab& S,
                                   MultiFab&       cp, 
                                   MultiFab&       hmix,
                                   MultiFab*       temp, 
                                   int             nGrow,
                                   int             calchmix,
                                   int             floor_spec)
{
    //
    // Notes:
    //  1) S has the same number of components as the state.
    //     However, he RhoH-th and the first-spec_th through last_spec-th
    //     components are assumed to have been converted to primitive
    //     form, i.e., they hold h and Y, not rho*h and rho*Y
    //  2) We assume that S, cp, and hmix all have the same set of boxes
    //     and the same number of ghost cells
    //  3) We only compute on the valid region regardless of the number of 
    //     ghost cells.
    //
    BL_ASSERT(S.nGrow() >= nGrow && cp.nGrow() >= nGrow);
    BL_ASSERT(S.nGrow() == cp.nGrow());

    const BoxArray& sgrids   = S.boxArray();
    bool            tmp_temp = temp != 0;
    int             nCompY   = last_spec - first_spec + 1;
    bool            tmp_spec = floor_spec && nCompY>0;
    if (tmp_spec)
	BoxLib::Error("Spec flooring not currently implemented");
    
    bool need_tmp_data = tmp_temp || tmp_spec;

    FArrayBox tmp;

    for (MFIter mfi(S); mfi.isValid(); ++mfi)
    {
	const int iGrid = mfi.index();
        Box       box   = BoxLib::grow(sgrids[iGrid],nGrow);

	int sCompY = -1, sCompT = -1;

	if (need_tmp_data)
	{
	    int nComp = 1 + nCompY;
	    tmp.resize(box,nComp);
	    sCompT = 0;
	    sCompY = 1;
	    if (tmp_temp)
	    {
		tmp.copy((*temp)[iGrid],0,sCompT,1);
	    }
            else
            {
		tmp.copy(S[iGrid],Temp,sCompT,1);
	    }
	    tmp.copy(S[iGrid],first_spec,sCompY,nCompY);
	}
        else
        {
	    sCompT = Temp;
	    sCompY = first_spec;
	}
	
	const FArrayBox& state = need_tmp_data ? tmp : S[iGrid];

	const int sCompCp = 0;
	getChemSolve().getCpmixGivenTY(cp[iGrid],state,state,box,sCompT,sCompY,sCompCp);

	if (calchmix)
	{
	    const int sCompH = 0;
	    getChemSolve().getHmixGivenTY(hmix[iGrid],state,state,box,sCompT,sCompY,sCompH);
	}
    }
}

void
HeatTransfer::compute_cp (Real      time,
                          MultiFab& cp)
{
    const int nGrow   = cp.nGrow();
    const int sComp   = std::min(std::min((int)Density,(int)Temp),first_spec);
    const int eComp   = std::max(std::max((int)Density,(int)Temp),first_spec+nspecies-1);
    const int nComp   = eComp - sComp + 1;
    const int sCompR  = Density - sComp;
    const int sCompT  = Temp - sComp;
    const int sCompY  = first_spec - sComp;
    const int sCompCp = 0;

    FArrayBox tmp;

    for (FillPatchIterator fpi(*this,cp,nGrow,time,State_Type,sComp,nComp);
         fpi.isValid();
         ++fpi)
    {
        //
        // Convert rho*Y to Y for this operation
        //
	const int  iGrid    = fpi.index();
	const Box& validBox = BoxLib::grow(grids[iGrid],nGrow);
	const Box& fabBox   = fpi().box();
	FArrayBox& state    = fpi();

        tmp.resize(validBox,1);
        tmp.copy(state,sCompR,0,1);
        tmp.invert(1);

        for (int k = 0; k < nspecies; k++)
            state.mult(tmp,0,sCompY+k,1);
	
	getChemSolve().getCpmixGivenTY(cp[iGrid],state,state,validBox,sCompT,sCompY,sCompCp);
    }
}

void
HeatTransfer::compute_cp (const FArrayBox& temp, 
                          const FArrayBox& species,
                          FArrayBox&       cp)
{
    const Box& box    = temp.box();
    const int nSpec   = last_spec - first_spec + 1;
    const int nComp   = nSpec + 1;
    const int sCompT  = 0;
    const int sCompY  = 1;
    const int sCompCp = 0;

    cp.resize(box,1);

    FArrayBox state(box,nComp);
    state.copy(temp,0,sCompT,1);
    state.copy(species,0,sCompY,nSpec);

    getChemSolve().getCpmixGivenTY(cp,state,state,box,sCompT,sCompY,sCompCp);
}

void
HeatTransfer::compute_rhohmix (Real      time,
                               MultiFab& rhohmix)
{
    const int ngrow  = 0; // We only do this on the valid region
    const int sComp  = std::min(std::min((int)Density,(int)Temp),first_spec);
    const int eComp  = std::max(std::max((int)Density,(int)Temp),first_spec+nspecies-1);
    const int nComp  = eComp - sComp + 1;
    const int sCompR = Density - sComp;
    const int sCompT = Temp - sComp;
    const int sCompY = first_spec - sComp;
    const int sCompH = 0;

    FArrayBox tmp;

    for (FillPatchIterator fpi(*this,rhohmix,ngrow,time,State_Type,sComp,nComp);
         fpi.isValid();
         ++fpi)
    {
        //
        // Convert rho*Y to Y for this operation
        //
	const int  iGrid    = fpi.index();
	const Box& validBox = grids[iGrid];
	const Box& fabBox   = fpi().box();
	FArrayBox& state    = fpi();

        tmp.resize(validBox,1);
        tmp.copy(state,sCompR,0,1);
        tmp.invert(1);

        for (int k = 0; k < nspecies; k++)
            state.mult(tmp,0,sCompY+k,1);
	
	getChemSolve().getHmixGivenTY(rhohmix[iGrid],state,state,validBox,
				      sCompT,sCompY,sCompH);
        //
        // Convert hmix to rho*hmix
        //
        rhohmix[iGrid].mult(state,validBox,sCompR,sCompH,1);
    }
}
 
void
HeatTransfer::compute_h (Real      time,
                         MultiFab& h)
{
    BL_ASSERT(h.nComp() == nspecies);
    BL_ASSERT(h.boxArray() == grids);

    const int nGrow = h.nGrow();
    const int sComp = Temp;
    const int nComp = 1;
    const int sCompT = 0;
    const int sCompH = 0;

    for (FillPatchIterator fpi(*this,h,nGrow,time,State_Type,sComp,nComp);
         fpi.isValid();
         ++fpi)
    {
	const Box box = BoxLib::grow(grids[fpi.index()],nGrow);

	BL_ASSERT(box == h[fpi.index()].box());

	getChemSolve().getHGivenT(h[fpi.index()],fpi(),box,sCompT,sCompH);
    }
}

void
HeatTransfer::setPlotVariables ()
{
    AmrLevel::setPlotVariables();

    const Array<std::string>& names = getChemSolve().speciesNames();

    ParmParse pp("ht");

    bool plot_ydot,plot_rhoY,plot_massFrac,plot_moleFrac,plot_conc;
    plot_ydot=plot_rhoY=plot_massFrac=plot_moleFrac=plot_conc = false;

    if (pp.query("plot_massfrac",plot_massFrac))
    {
        if (plot_massFrac)
        {
            for (int i = 0; i < names.size(); i++)
            {
                const std::string name = "Y("+names[i]+")";
                parent->addDerivePlotVar(name);
            }
        }
        else
        {
            for (int i = 0; i < names.size(); i++)
            {
                const std::string name = "Y("+names[i]+")";
                parent->deleteDerivePlotVar(name);
            }
        }
    }

    if (pp.query("plot_molefrac",plot_moleFrac))
    {
        if (plot_moleFrac)
            parent->addDerivePlotVar("molefrac");
        else
            parent->deleteDerivePlotVar("molefrac");
    }

    if (pp.query("plot_concentration",plot_conc))
    {
        if (plot_conc)
            parent->addDerivePlotVar("concentration");
        else
            parent->deleteDerivePlotVar("concentration");
    }

    if (pp.query("plot_ydot",plot_ydot))
    {
        if (plot_ydot)
        {
            for (int i = 0; i < names.size(); i++)
            {
                const std::string name = "d[Y("+names[i]+")]/dt";
                parent->addStatePlotVar(name);
            }
        }
        else
        {
            for (int i = 0; i < names.size(); i++)
            {
                const std::string name = "d[Y("+names[i]+")]/dt";
                parent->deleteStatePlotVar(name);
            }
        }
    }
  
    if (pp.query("plot_rhoY",plot_rhoY))
    {
        if (plot_rhoY)
        {
            for (int i = 0; i < names.size(); i++)
            {
                const std::string name = "rho.Y("+names[i]+")";
                parent->addStatePlotVar(name);
            }
        }
        else
        {
            for (int i = 0; i < names.size(); i++)
            {
                const std::string name = "rho.Y("+names[i]+")";
                parent->deleteStatePlotVar(name);
            }
        }
    }

    if (verbose && ParallelDescriptor::IOProcessor())
    {
        std::cout << "\nState Plot Vars: ";

        std::list<std::string>::const_iterator li = parent->statePlotVars().begin();

        for ( ; li != parent->statePlotVars().end(); ++li)
            std::cout << *li << ' ';
        std::cout << '\n';

        std::cout << "\nDerive Plot Vars: ";

        li = parent->derivePlotVars().begin();

        for ( ; li != parent->derivePlotVars().end(); ++li)
            std::cout << *li << ' ';
        std::cout << '\n';
    }
}

void
HeatTransfer::writePlotFile (const std::string& dir,
                             std::ostream&  os,
                             VisMF::How     how)
{
    if ( ! Amr::Plot_Files_Output() ) return;
    //
    // Note that this is really the same as its NavierStokes counterpart,
    // but in order to add diagnostic MultiFabs into the plotfile, code had
    // to be interspersed within this function.
    //
    int i, n;
    //
    // The list of indices of State to write to plotfile.
    // first component of pair is state_type,
    // second component of pair is component # within the state_type
    //
    std::vector<std::pair<int,int> > plot_var_map;
    for (int typ = 0; typ < desc_lst.size(); typ++)
        for (int comp = 0; comp < desc_lst[typ].nComp();comp++)
            if (parent->isStatePlotVar(desc_lst[typ].name(comp)) &&
                desc_lst[typ].getType() == IndexType::TheCellType())
                plot_var_map.push_back(std::pair<int,int>(typ,comp));

    int num_derive = 0;
    std::list<std::string> derive_names;
    const std::list<DeriveRec>& dlist = derive_lst.dlist();

    for (std::list<DeriveRec>::const_iterator it = dlist.begin();
         it != dlist.end();
         ++it)
    {
        if (parent->isDerivePlotVar(it->name()))
	{
            derive_names.push_back(it->name());
            num_derive += it->numDerive();
	}
    }

    int num_auxDiag = 0;
    for (std::map<std::string,MultiFab*>::const_iterator it = auxDiag.begin(); it!=auxDiag.end(); ++it)
    {
        num_auxDiag += it->second->nComp();
    }

    int n_data_items = plot_var_map.size() + num_derive + num_auxDiag;
    Real cur_time = state[State_Type].curTime();

    if (level == 0 && ParallelDescriptor::IOProcessor())
    {
        //
        // The first thing we write out is the plotfile type.
        //
        os << thePlotFileType() << '\n';

        if (n_data_items == 0)
            BoxLib::Error("Must specify at least one valid data item to plot");

        os << n_data_items << '\n';

	//
	// Names of variables -- first state, then derived
	//
	for (i =0; i < plot_var_map.size(); i++)
        {
	    int typ  = plot_var_map[i].first;
	    int comp = plot_var_map[i].second;
	    os << desc_lst[typ].name(comp) << '\n';
        }

	for (std::list<std::string>::const_iterator it = derive_names.begin();
             it != derive_names.end();
             ++it)
        {
	    const DeriveRec* rec = derive_lst.get(*it);
	    for (i = 0; i < rec->numDerive(); i++)
                os << rec->variableName(i) << '\n';
        }
        //
        // Hack in additional diagnostics.
        //
        for (std::map<std::string,Array<std::string> >::const_iterator it = auxDiag_names.begin();
             it!=auxDiag_names.end(); ++it)
        {
            for (i=0; i<it->second.size(); ++i)
                os << it->second[i] << '\n';
        }

        os << BL_SPACEDIM << '\n';
        os << parent->cumTime() << '\n';
        int f_lev = parent->finestLevel();
        os << f_lev << '\n';
        for (i = 0; i < BL_SPACEDIM; i++)
            os << Geometry::ProbLo(i) << ' ';
        os << '\n';
        for (i = 0; i < BL_SPACEDIM; i++)
            os << Geometry::ProbHi(i) << ' ';
        os << '\n';
        for (i = 0; i < f_lev; i++)
            os << parent->refRatio(i)[0] << ' ';
        os << '\n';
        for (i = 0; i <= f_lev; i++)
            os << parent->Geom(i).Domain() << ' ';
        os << '\n';
        for (i = 0; i <= f_lev; i++)
            os << parent->levelSteps(i) << ' ';
        os << '\n';
        for (i = 0; i <= f_lev; i++)
        {
            for (int k = 0; k < BL_SPACEDIM; k++)
                os << parent->Geom(i).CellSize()[k] << ' ';
            os << '\n';
        }
        os << (int) Geometry::Coord() << '\n';
        os << "0\n"; // Write bndry data.
    }
    // Build the directory to hold the MultiFab at this level.
    // The name is relative to the directory containing the Header file.
    //
    static const std::string BaseName = "/Cell";

    std::string Level = BoxLib::Concatenate("Level_", level, 1);
    //
    // Now for the full pathname of that directory.
    //
    std::string FullPath = dir;
    if (!FullPath.empty() && FullPath[FullPath.length()-1] != '/')
        FullPath += '/';
    FullPath += Level;
    //
    // Only the I/O processor makes the directory if it doesn't already exist.
    //
    if (ParallelDescriptor::IOProcessor())
        if (!BoxLib::UtilCreateDirectory(FullPath, 0755))
            BoxLib::CreateDirectoryFailed(FullPath);
    //
    // Force other processors to wait till directory is built.
    //
    ParallelDescriptor::Barrier();

    if (ParallelDescriptor::IOProcessor())
    {
        os << level << ' ' << grids.size() << ' ' << cur_time << '\n';
        os << parent->levelSteps(level) << '\n';

        for (i = 0; i < grids.size(); ++i)
        {
            RealBox gridloc = RealBox(grids[i],geom.CellSize(),geom.ProbLo());
            for (n = 0; n < BL_SPACEDIM; n++)
                os << gridloc.lo(n) << ' ' << gridloc.hi(n) << '\n';
        }
        //
        // The full relative pathname of the MultiFabs at this level.
        // The name is relative to the Header file containing this name.
        // It's the name that gets written into the Header.
        //
        if (n_data_items > 0)
        {
            std::string PathNameInHeader = Level;
            PathNameInHeader += BaseName;
            os << PathNameInHeader << '\n';
        }
    }
    //
    // We combine all of the multifabs -- state, derived, etc -- into one
    // multifab -- plotMF.
    // NOTE: we are assuming that each state variable has one component,
    // but a derived variable is allowed to have multiple components.
    int       cnt   = 0;
    int       ncomp = 1;
    const int nGrow = 0;
    MultiFab  plotMF(grids,n_data_items,nGrow);
    MultiFab* this_dat = 0;
    //
    // Cull data from state variables -- use no ghost cells.
    //
    for (i = 0; i < plot_var_map.size(); i++)
    {
	int typ  = plot_var_map[i].first;
	int comp = plot_var_map[i].second;
	this_dat = &state[typ].newData();
	MultiFab::Copy(plotMF,*this_dat,comp,cnt,ncomp,nGrow);
	cnt+= ncomp;
    }
    //
    // Cull data from derived variables.
    // 
    Real plot_time;

    if (derive_names.size() > 0)
    {
	for (std::list<std::string>::const_iterator it = derive_names.begin();
             it != derive_names.end();
             ++it) 
	{
            if (*it == "avg_pressure" ||
                *it == "gradpx"       ||
                *it == "gradpy"       ||
                *it == "gradpz") 
            {
                if (state[Press_Type].descriptor()->timeType() == 
                    StateDescriptor::Interval) 
                {
                    plot_time = cur_time;
                }
                else
                {
                    int f_lev = parent->finestLevel();
                    plot_time = getLevel(f_lev).state[Press_Type].curTime();
                }
            }
            else
            {
                plot_time = cur_time;
            } 
	    const DeriveRec* rec = derive_lst.get(*it);
	    ncomp = rec->numDerive();
	    MultiFab* derive_dat = derive(*it,plot_time,nGrow);
	    MultiFab::Copy(plotMF,*derive_dat,0,cnt,ncomp,nGrow);
	    delete derive_dat;
	    cnt += ncomp;
	}
    }
    //
    // Cull data from diagnostic multifabs.
    //
    for (std::map<std::string,MultiFab*>::const_iterator it = auxDiag.begin(); it!=auxDiag.end(); ++it)
    {
        int nComp = it->second->nComp();
        MultiFab::Copy(plotMF,*it->second,0,cnt,nComp,nGrow);
        cnt += nComp;
    }
    //
    // Use the Full pathname when naming the MultiFab.
    //
    std::string TheFullPath = FullPath;
    TheFullPath += BaseName;
    VisMF::Write(plotMF,TheFullPath,how);
}

//--------------------------------------------------------------------------
//--------------------------------------------------------------------------
//  SDC stuff 
//--------------------------------------------------------------------------
//--------------------------------------------------------------------------

// Amr.cpp::timeStep() calls advance.  I inserted an if(use_sdc) there.

// Need to insert some initialization test to make sure that when use_sdc
// velocity advection is done separately.  do_mom_diff = 0???
// also need num_divu_iters = 0

// Change advance_setup() to take out mf copy???

Real
HeatTransfer::advance_sdc (Real time,
			   Real dt,
			   int  iteration,
			   int  ncycle)
{
    if (level == 0)
    {
        crse_dt = dt;
        int thisLevelStep = parent->levelSteps(0);
        FORT_SET_COMMON(&time,&thisLevelStep);
    }

    if (verbose && ParallelDescriptor::IOProcessor())
    {
        std::cout << "SDC Advancing level "    << level
                  << " : starting time = " << time
                  << " with dt = "         << dt << std::endl;
    }

    // initialize a lot of stuff i don't have to worry about
    // aofs is declared here
    // sdcForce, DofS, I_R, and I_AD also built here
    // rho_avg is set
    // calculates time n viscosity & diffusivity:
    //    calculates mixture averaged diffusivity (D_m) for each species
    //    load into NavierStokes::diffn_cc as Rho*D_m
    //    calculate thermal diffusivity; load into NS::diffn_cc
    //    calculate enthalpy diffusivity (thermal diffusivity/c_p,mix)
    // initializes n+1 vals to time n vals
    advance_setup_sdc(time,dt,iteration,ncycle);
    std::cout<<"after sdc setup"<<std::endl;

    if (do_check_divudt)
        checkTimeStep(dt);
    
    MultiFab& S_new = get_new_data(State_Type);
    MultiFab& S_old = get_old_data(State_Type);
    bool use_spec_prev_time_diffusion = false;
    //    VisMF::Write(S_old,"sold_sdc");

    //Diagnostics for SDC
    MultiFab difference(grids,NUM_STATE,0);
    MultiFab::Copy(difference,S_old,0,0,NUM_STATE,0);
    MultiFab I_change(grids,NUM_STATE,0);
    //I_AD initialized to zero in advance_setup
    MultiFab::Copy(I_change,I_AD[0],0,0,NUM_STATE,0);

    //
    // Set flag so that aux_boundary_data is not used.
    // should not be needed with SDC
    FillPatchedOldState_ok = true;
    //
    // Compute traced states for normal comp of velocity at half time level.
    //
    Real dt_test = 0.0, dummy = 0.0;    
    dt_test = predict_velocity(dt,dummy);
    
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): at start of time step\n";

    // This just writes out some statistics
    // Does nothing if verbose = false
    // move if() up into this function?
    temperature_stats(S_old);

    const Real prev_time = state[State_Type].prevTime();
    const Real cur_time  = state[State_Type].curTime();

    //
    // Do MAC projection and update edge velocities.
    //
    if (do_mac_proj) 
    {
        int havedivu = 1;
        MultiFab* mac_rhs = create_mac_rhs(time,dt);
        mac_project(time,dt,S_old,mac_rhs,havedivu);
        delete mac_rhs;
    }

    // velocity advection was here.  moved to after scalar stuff with 
    // no effect on answer/result

    //CEG:: I think this function is strictly for the aux bdry stuff
    //   and I can remove it
    // Set old-time boundary data for RhoH
    set_overdetermined_boundary_cells(time);

    BL_ASSERT(S_new.boxArray() == S_old.boxArray());

    const int nScalDiffs = NUM_STATE-BL_SPACEDIM-1;

    ///////////////////////////////
    // Update energy and species //
    ///////////////////////////////

    //
    // Provisional Solution
    //

    //CEG:: should insert some kind of if()??
    // Get Diffusion(S) at time n; this DOES NOT have dt multipied in
    //   returns Diff(S)/vol
    getViscTerms_sdc(*sdcForce,first_spec,nspecies,prev_time);
    // save a copy of D(t^n)
    MultiFab::Copy(DofS[0],*sdcForce,first_spec,first_spec,nspecies,0); 
    //VisMF::Write(DofS[0],"dofs_sdc");    
    //    getForce_sdc(first_spec,nspecies,nGrowForce);

    // Godunov-extrapolate states to cell edges
    // this function also computes the time n fluxes, 
    //  and from them the diffusion terms at time n, 
    //  they're saved SpecDiffFlux (HT member variable). 
    // Rho edge states are gotten from Sum(rho Y_m)
    // RhoH edge states are gotten from rhoH = Sum(rho Y_m h_m)
    // 
    // with my current problem, velocity edge
    //  states must be computed elsewhere--probably velcity_advection()
    //
    // will pull external forces from getForce
    // and add in sdc terms in sdcForce.  if there aren't any, must 
    // be sure to put zero into sdcForce
    compute_edge_states_sdc(dt);

//     std::ofstream edge_fab("edgex_sdc");
//     (*EdgeState[0])[0].writeOn(edge_fab);
//     edge_fab.close();
    
//     std::ofstream edgey_fab("edgey_sdc");
//     (*EdgeState[1])[0].writeOn(edgey_fab);
//     edgey_fab.close();


// loop over quad pts here
// need to make sure that rho_ptime and rho_ctime are filled properly
// refer to make_rho_curr_time/make_rho_prev_time
// make_rho half_time only looks at what's in the saved mf's

    // Compute advective fluxes divergences, where possible
    const int first_scalar = Density;
    const int last_scalar = first_scalar + NUM_SCALARS - 1;
    //    bool do_adv_reflux = true;
    bool do_adv_reflux = false;

    // Compute advection:
    //  AofS is computed without mult by dt but divided by the vol
    //  This does not update/change Snew 
    // Do RhoH later
    if (RhoH > first_scalar)
	scalar_advection(dt,first_scalar,RhoH-1,do_adv_reflux);
    if (RhoH < last_scalar)
	scalar_advection(dt,RhoH+1,last_scalar,do_adv_reflux);

//     NUM_STATE    = 16
//     nScalDiffs   = 13
//     NUM_SCALARS  = 14
//     first_scalar = 2
//     last_scalar  = 15

    // Save rho used in rho-states, needed for replacing with new one
    //  NOTE: WE LOAD/USE GROW CELLS HERE SO WE CAN FIX BOUNDARY 
    //    DATA AS WELL
    //
    // CEG:: Using Rho_hold here might not be neccessary any more
    //    was it to get multi grid to converge faster???
    // Why do need to use MFIter--Don't.  this does the same thing as
    //  MF::Copy
    MultiFab Rho_hold(grids,1,LinOp_grow);

    BL_ASSERT(LinOp_grow == 1);

    for (MFIter mfi(*rho_ctime); mfi.isValid(); ++mfi)
    {
        const Box box = BoxLib::grow(mfi.validbox(),LinOp_grow);
        Rho_hold[mfi.index()].copy((*rho_ctime)[mfi],box,0,box,0,1);
    }

    // Compute new and half-time densities.
    // CEG: 
    // scalar_update(dt, first_scalar, last_scalar, corrector)
    //  so only calling on Density here
    // Updates rho by solving continuity 
    //  not doing sdc on rho directly, just rhoY_m's
    scalar_advection_update(dt,Density,Density);
    //    VisMF::Write(S_new,"rho_update_sdc");    

    // Set saved rho at current time.
    make_rho_curr_time();

    // Reset rho-states to contain new rho.
    reset_rho_in_rho_states(Rho_hold,cur_time,first_scalar+1,NUM_SCALARS-1);

    int corrector = 0;

    // Predict n+1 coeffs using n coeffs
    temp_update(dt,corrector); 
    temperature_stats(S_new);
        
    //CEG:: Why do this here?  Do diffusivities only depend on Temp
    //   and rho (should be independent of rho)
    // Calculate Diffusivities at time n+1
    calcDiffusivity(cur_time,dt,iteration,ncycle,Density+1,nScalDiffs);
//     VisMF::Write(*diffnp1_cc,"Dmn1_sdc");
//     abort();

    // Update species with advection (just adding up terms here):
    //
    //      snew = sold - dt*aofs + dt*ext_forcing_terms
    //  
    scalar_advection_update(dt, first_spec, last_spec);
    //    VisMF::Write(*aofs,"adv_sdc");     

    // Update species with diffusion (calling multi-grid in here):
    //
    //      snew = sold - dt*aofs + dt*Diffsn^n+1 + dt*ext_forcing
    //                  + dt*sdc_forcing
    //
    //  diffusion looks in sdcForce for any sdc forcing terms
    //   make sure to have something in there.
    //   set to zero for sdc
    //  Contains flux readjustment procedure so mass is explicitly
    //   conserved.
    *sdcForce = 0.0;
    differential_spec_diffusion_update_sdc(dt, corrector);
//      VisMF::Write(S_new,"spec_update_sdc");    
//      abort();

    //!!!! getViscTerms changes SpecDiffusionFlux -- Ask marc
    // Compute Diffn^n+1 for species
    //    getViscTerms_sdc(DofS[1],first_spec,nspecies,cur_time);
 
    // 
    // Update RhoH
    //
    // RhoH BC's to see new Y's at n+1
    set_overdetermined_boundary_cells(time + dt); 

    // Don't actually need this yet
    // Compute Diffsn(H)^n
    getRhoHViscTerms(DofS[0],RhoH,prev_time);
    
    // Compute advection (AofS for rhoH)
    rhoh_advection_sdc(dt,do_adv_reflux);    
//     VisMF::Write(*aofs,"adv_sdc");   
//     (*aofs).mult(dt,RhoH,1);  
//     MultiFab::Add(*aofs,RhoH_NULN_terms[1],0,RhoH,1,0);
//     MultiFab::Add(*aofs,RhoH_NULN_terms[0],0,RhoH,1,0);
//    VisMF::Write(*aofs,"aofs_sdc");     

    // Update rhoH with advection (just adding up terms here):
    //
    //      snew = sold - dt*aofs + dt*ext_forcing_terms
    //  
    scalar_advection_update(dt,RhoH,RhoH);
    //    VisMF::Write(S_new,"sa_sdc");     
 //      MultiFab::Add(RhoH_NULN_terms[0],RhoH_NULN_terms[1],0,0,1,0);
//       RhoH_NULN_terms[0].mult(dt,0,1);
//       MultiFab::Subtract(S_new,RhoH_NULN_terms[0],0,RhoH,1,0);
//	VisMF::Write(S_new,"sa_sdc");     



    // Compute Non-unity Lewis number (NULN) terms
    //  Actually computing -NULN
    //
    //CEG:: compute edge states overwrites whatever is
    //  in EdgeState.. Need to either compute NULN_n again, or save the 
    //  flux somewhere.
    bool save_flux = true;
    compute_rhoh_NULN_terms(prev_time, dt, do_adv_reflux,
			    RhoH_NULN_terms[0],save_flux);
    save_flux = false;
    compute_rhoh_NULN_terms(cur_time, dt, do_adv_reflux,
			    RhoH_NULN_terms[1]);

    // put NULN terms into sdcForce using implicit euler
    MultiFab::Copy(*sdcForce,RhoH_NULN_terms[0],0,RhoH,1,0);
    //FIXME debugging
    // (*sdcForce).mult(-1, 0, 1);
    // using a crank-nicolson discretization;
    //  needs a factor of -1/2 b/c storing -NULN
     MultiFab::Add(*sdcForce,RhoH_NULN_terms[1],0,RhoH,1,0);
     (*sdcForce).mult(-0.5,RhoH,1);
     //REMOVE ME debugging
     //(*sdcForce).mult(-1.,RhoH,1);
       MultiFab::Subtract(*aofs,*sdcForce,RhoH,RhoH,1,0);
       //     VisMF::Write(*aofs,"adv_sdc");
       //       abort();

    // Update rhoH with diffusion (calling multi-grid in here) and
    //  NULN terms:
    //
    //      snew = sold - dt*aofs + dt*Diffsn^n+1
    //                  + dt*ext_forcing + dt*sdc_forcing
    //
    //  diffusion looks in sdcForce for NULN terms and any sdc forcing 
    //   terms 
    rhoh_diffusion_update_sdc(dt,corrector);
    //    VisMF::Write(S_new,"rhdiff_sdc");  
    //      abort();   
//     VisMF::Write(S_new,"snew_sdc");     

    // Compute Diffsn(rhoH)^n+1
    getRhoHViscTerms(DofS[1],RhoH,cur_time);

    getViscTerms_sdc(DofS[1],first_spec,nspecies,cur_time);

    //
    //React
    //

    // Save a copy of rho_new or set up chem_sdc to take rho separately
    // CEG:: think about ghost cells.
 //    MultiFab::Copy(Rho_hold,*rho_ctime,0,0,1,1);

//     MultiFab::Copy(S_new,S_old,first_scalar,first_scalar,NUM_SCALARS,0);

//     int sdc_flag = 0; //doing provisional solution 
//     chem_sdc(S_new,dt,sdc_flag,0);

//     // need to think about ghost cells.
//     MultiFab::Copy(S_new,Rho_hold,0,Density,1,1);

    //Make I_R
    // because code will mult forcing terms by dt later, 
    // have to actually compute I_R/sdc_dt
    //    make_I_R(j,dt);
    
    // change in state over timestep
    Real comp_min, comp_max;
    MultiFab::Subtract(difference,S_new,0,0,NUM_STATE,0);
    if (ParallelDescriptor::IOProcessor())
    {
      std::cout<<"Change in state over timestep, with provisional soln"<<std::endl;
      std::cout<<"comp     min    max"<<std::endl;
    }  
    for(int i = 0; i < NUM_STATE; i++)
    {
      comp_min = difference.min(i);
      comp_max = difference.max(i);
      if (ParallelDescriptor::IOProcessor())
	std::cout<<"comp "<<i<<": "<<comp_min<<" "<<comp_max<<std::endl;
    }
    if (ParallelDescriptor::IOProcessor())
      std::cout<<std::endl;
    MultiFab::Copy(difference,S_new,0,0,NUM_STATE,0);

    ////////////////////
    // SDC iterations //
    ////////////////////
    for (int n_sdc = 1; n_sdc <= sdc_iters; n_sdc++)        
    { 
      // Recompute DofS(n+1) 
      //  species
      getViscTerms_sdc(DofS[2],first_spec,nspecies,cur_time);
      //  RhoH
      getRhoHViscTerms(DofS[2],RhoH,cur_time);
      //  NULN terms 
      compute_rhoh_NULN_terms(cur_time, dt, do_adv_reflux,
			      RhoH_NULN_terms[2]);

      //make I_AD
      make_I_AD();

      // Update Temp
      RhoH_to_Temp(S_new);
      //VisMF::Write(S_new,"temp_update");     
      temperature_stats(S_new);

      // Update Diffusivities
      //
      //CEG::Put this after I_AD for now; doing earlier might adversely 
      // affect stability
      calcDiffusivity(cur_time,dt,iteration,ncycle,Density+1,nScalDiffs);

//       MultiFab::Subtract(I_change,I_AD[0],0,0,NUM_STATE,0);
// //       I_change.mult(dt,first_spec,nspecies);
// //       I_change.mult(dt,first_spec,nspecies);
//hangs in parallel, FIX if(IOProc) statement
//       if (ParallelDescriptor::IOProcessor())
//       {
// 	std::cout<<"Change in I_AD (first time gives magnitude)"<<std::endl;
// 	std::cout<<"comp     min    max"<<std::endl;
// 	for(int i = first_spec; i < first_spec+nspecies; i++)
//         {
// //  	  comp_min = DofS[0].min(i);
// // 	  comp_max = DofS[0].max(i);
// // 	  std::cout<<"comp "<<i<<": "<<comp_min<<" "<<comp_max<<std::endl;
// // 	  comp_min = DofS[1].min(i);
// // 	  comp_max = DofS[1].max(i);
// // 	  std::cout<<"comp "<<i<<": "<<comp_min<<" "<<comp_max<<std::endl;
// 	  comp_min = I_change.min(i);
// 	  comp_max = I_change.max(i);
// 	  std::cout<<"comp "<<i<<": "<<comp_min<<" "<<comp_max<<std::endl;
// 	  if(i != first_spec)
// 	    MultiFab::Add(I_change,I_AD[0],i,first_spec,1,0);
// 	  else MultiFab::Copy(I_change,I_AD[0],i,first_spec,1,0);
// 	}
//       }
//       std::cout<<"sum I_AD over species"<<std::endl;
//       comp_min = I_change.min(first_spec);
//       comp_max = I_change.max(first_spec);
//       std::cout<<comp_min<<" "<<comp_max<<std::endl;

//       MultiFab::Copy(I_change,I_AD[0],0,0,NUM_STATE,0);

      if (ParallelDescriptor::IOProcessor())
	std::cout<<"Inside SDC iters... n_sdc= "<<n_sdc<<std::endl;

      // for radau will need to check that rho_ptime has the right data 
      //  in it. 
      // am i wastecully recomputing rho_half?

      // load I_AD + I_R into sdcForces and fill ghost cells  
      //   use 0th order extrapolation if needed
      make_advection_sdcForce(first_spec,nspecies);

      // will pull external forces from getForce
      // and add in sdc terms in sdcForce. 
      // maybe should let rho passed into getForce = rho_half???
      compute_edge_states_sdc(dt);

      if (n_sdc == sdc_iters)
      {
	corrector = 1;
	do_adv_reflux = true;

	//Don't need any more
// 	//CEG:: should probably change this to store the fluxes and
// 	//  then increment EdgeState here.
// 	compute_rhoh_NULN_terms(prev_time, dt, do_adv_reflux,
// 				RhoH_NULN_terms[0]);

	// RhoRT is also updated in this function
	tracer_update(dt,corrector);
      }


      //
      //  Update Rho and Species
      //

      // Recompute aofs
      if (RhoH > first_scalar)
	scalar_advection(dt,first_scalar,RhoH-1,do_adv_reflux);
      if (RhoH < last_scalar)
	scalar_advection(dt,RhoH+1,last_scalar,do_adv_reflux);

      // loop over quad pts here?

      for (MFIter mfi(*rho_ctime); mfi.isValid(); ++mfi)
      {
        const Box box = BoxLib::grow(mfi.validbox(),LinOp_grow);
        Rho_hold[mfi.index()].copy((*rho_ctime)[mfi],box,0,box,0,1);
      }

       // Update new and half-time density.
      scalar_advection_update(dt,Density,Density);

      // Set to rho in State_new (at current time).
      make_rho_curr_time();

      //CEG:: do I need this here???
      reset_rho_in_rho_states(Rho_hold,cur_time,first_scalar+1,
 			      NUM_SCALARS-1);

//       VisMF::Write(S_new,"sn_sdc");
//       abort();
      // Update species with snew = sold - dt*aofs + dt*extForces
      scalar_advection_update(dt, first_spec, last_spec);
      //VisMF::Write(S_new,"adv_update3");     
//       VisMF::Write(S_new,"sn_sdc"); 
//       abort();

      // fill sdcForce
      make_diffusion_sdcForce(first_spec,nspecies);
      //FIXME debugging
      (*sdcForce).setVal (0.);

      //for debugging
//       VisMF::Write(DofS[0],"d0");
//      *sdcForce = 0.0;
      differential_spec_diffusion_update_sdc(dt, corrector);
//       VisMF::Write(*SpecDiffusionFluxnp1[0],"specfluxx_sdc");
//       VisMF::Write(*SpecDiffusionFluxnp1[1],"specfluxy_sdc");
//       VisMF::Write(*SpecDiffusionFluxn[0],"specfluxxn_sdc");
//       VisMF::Write(*SpecDiffusionFluxn[1],"specfluxyn_sdc");

//       VisMF::Write(S_new,"snew_sdc"); 
//       abort();    

      // !!!! getViscTerms changes SpecDiffusionFlux
      //Recompute D(t^n+1)
      //      getViscTerms_sdc(DofS[1],first_spec,nspecies,cur_time);


      //
      // Update RhoH
      //

      // RhoH BC's to see new Y's at n+1
      set_overdetermined_boundary_cells(time + dt);
      rhoh_advection_sdc(dt,do_adv_reflux);    

      scalar_advection_update(dt,RhoH,RhoH);
      //     VisMF::Write(*aofs,"aofs_sdc");     

      //FIXME debugging
//       compute_rhoh_NULN_terms(prev_time, dt, do_adv_reflux,
// 				RhoH_NULN_terms[0]);

      compute_rhoh_NULN_terms(cur_time, dt, do_adv_reflux,
			      RhoH_NULN_terms[1]);

      //      MultiFab::Add(RhoH_NULN_terms[0],RhoH_NULN_terms[1],0,0,1,0);  
    
      make_diffusion_sdcForce(RhoH,1);
      // Take care of the NULN terms
      //MultiFab::Add(*sdcForce,RhoH_NULN_terms[1],0,RhoH,1,0);      

      rhoh_diffusion_update_sdc(dt,corrector);
//       VisMF::Write(S_new,"rhdiff_sdc");     
//       abort();

      // Recompute Diffsn(rhoH)^n+1
      getRhoHViscTerms(DofS[1],RhoH,cur_time);
      // Recompute D(t^n+1)
      getViscTerms_sdc(DofS[1],first_spec,nspecies,cur_time);


      //
      //React
      //

//       MultiFab::Copy(Rho_hold,*rho_ctime,0,0,1,1);
	
//       MultiFab::Copy(S_new,S_old,first_scalar,first_scalar,NUM_SCALARS,0);
      
//       int sdc_flag = 1; //doing SDC solution 
//       chem_sdc(S_new,dt,sdc_flag,0);
      
//       // need to think about ghost cells.
//       MultiFab::Copy(S_new,Rho_hold,0,Density,1,1);


      //Recompute I_R

    
      //Compute the change in the soln at n+1
      MultiFab::Subtract(difference,S_new,0,0,NUM_STATE,0);
      if (ParallelDescriptor::IOProcessor())
      {
	std::cout<<"Change in soln at t^n+1"<<std::endl;
	std::cout<<"comp     min    max"<<std::endl;
      }
      for(int i = 0; i < NUM_STATE; i++)
      {
	comp_min = difference.min(i);
	comp_max = difference.max(i);
	if (ParallelDescriptor::IOProcessor())
	  std::cout<<"comp "<<i<<": "<<comp_min<<" "<<comp_max<<std::endl;
      }
      MultiFab::Copy(difference,S_new,0,0,NUM_STATE,0);
 
    }//end sdc_iters loop
 
    Rho_hold.clear();
    if (ParallelDescriptor::IOProcessor())  
      std::cout<<"done with sdc_iters loop...\n"<<std::endl;

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): after scalar_update\n";

    // return new-time
    // data, and provide a predicted Ydot for the velocity
    // constraint.  We write the result over the new state, but only care
    // about stuff in the valid region.
    //
    // CEG:: do I need to put data in ydot for other funcs to use???

    // REMOVE ME!!!   second half of strang was here
    // inserting hack_nochem from strang_chem()
//     MultiFab *ydot_tmp = &get_new_data(Ydot_Type);
//     // first component of Ydot corres. to first_spec
//     const int dCompYdot = 0;      
//     ydot_tmp->setVal(0,dCompYdot,nspecies);

    RhoH_to_Temp(S_new); 
    // This just writes out some statistics.
    // Does nothing if verbose = false    
    temperature_stats(S_new);

    // S appears in rhs of the velocity update, so we better do it now.
    // (be sure to use most recent version of state to get
    // viscosity/diffusivity).
    //
    calcDiffusivity(cur_time,dt,iteration,ncycle,Density+1,nScalDiffs,true);

    // CEG: do_mom_diff = 0
    if (do_mom_diff == 0)
        velocity_advection(dt);

    // Set the dependent value of RhoRT to be the thermodynamic pressure.  
    // By keeping this in
    // the state, we can use the average down stuff to be sure that 
    // RhoRT_avg is avg(RhoRT),
    // not ave(Rho)avg(R)avg(T), which seems to give the p-relax stuff 
    // in the mac Rhs troubles.
    //
    setThermoPress(cur_time);

    calc_divu(time+dt, dt, get_new_data(Divu_Type));

    if (!NavierStokes::initial_step && level != parent->finestLevel())
    {
        //
        // Set new divu to old div + dt*dsdt_old where covered by fine.
        //
        BoxArray crsndgrids = getLevel(level+1).grids;
        crsndgrids.coarsen(fine_ratio);
            
        MultiFab& divu_new = get_new_data(Divu_Type);
        MultiFab& divu_old = get_old_data(Divu_Type);
        MultiFab& dsdt_old = get_old_data(Dsdt_Type);
            
        for (MFIter mfi(divu_new); mfi.isValid();++mfi)
        {
            for (int i=0; i<crsndgrids.size(); ++i)
            {
                const Box ovlp = crsndgrids[i] & mfi.validbox();
                if (ovlp.ok())
                {
                    divu_new[mfi].copy(dsdt_old[mfi],ovlp,0,ovlp,0,1);
                    divu_new[mfi].mult(dt,ovlp,0,1);
                    divu_new[mfi].plus(divu_old[mfi],ovlp,0,0,1);
                }
            }
        }
    }
        
    calc_dsdt(time, dt, get_new_data(Dsdt_Type));

    if (NavierStokes::initial_step)
      MultiFab::Copy(get_old_data(Dsdt_Type),get_new_data(Dsdt_Type),0,0,1,0);
    //
    // Add the advective and other terms to get velocity (or momentum) at t^{n+1}.
    //
    velocity_update(dt);

    // deletes aofs and u_mac
    advance_cleanup_sdc(dt,iteration,ncycle);
    //
    // Increment rho average.
    //
    if (!initial_step)
    {
        if (level > 0)
        {
            Real alpha = 1.0/Real(ncycle);
            if (iteration == ncycle)
                alpha = 0.5/Real(ncycle);
            incrRhoAvg(alpha);
        }
        //
        // Do a level project to update the pressure and velocity fields.
        //
        level_projector(dt,time,iteration);

        if (level > 0 && iteration == 1) p_avg->setVal(0);
    }
    //
    // Update estimate for allowable time step.
    //
    dt_test = std::min(dt_test, estTimeStep());
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::advance(): at end of time step\n";

    temperature_stats(S_new);

    //    VisMF::Write(S_new,"cppMF");     

    //need if(S_new.defined(0)) to run in parallel
//     std::ofstream edge_fab("snew_sdc");
//     S_new[0].writeOn(edge_fab);
//     edge_fab.close();

    return dt_test;
}

void
HeatTransfer::advance_setup_sdc (Real time,
                             Real dt,
                             int  iteration,
                             int  ncycle)
{
  //CEG: does a bunch of stuff I don't need to worry about
  // computes Diffusivity at time n and then copies this val into 
  // Diffus at t_n+1 as an initialization
  // declares aofs
  // computing species Diffusivity again is kinda a waste.
  //should i change this?
  // also call make_rho_prev_time
    NavierStokes::advance_setup(time, dt, iteration, ncycle);

    //
    // Make sure the new state has values so that c-n works
    // in the predictor (of the predictor)--rbp.
    //
    for (int k = 0; k < num_state_type; k++)
    {
        MultiFab& nstate = get_new_data(k);
        MultiFab& ostate = get_old_data(k);

        MultiFab::Copy(nstate,ostate,0,0,nstate.nComp(),0);
    }
    // then would need to chage this to copy from rho_n
    make_rho_curr_time();

    //not using aux bdry data anymore, remove
//#ifndef NDEBUG
//    aux_boundary_data_old.setVal(BL_BOGUS);
//    aux_boundary_data_new.setVal(BL_BOGUS);
//#endif
    //
    // Set a dumbbell flag to help avoid stupid mistakes
    //
    for (int i = 0; i < spec_diffusion_flux_computed.size(); ++i)
	spec_diffusion_flux_computed[i] = HT_None;

    (*aofs).setVal(0.0);
    //sdc additions:
    //CEG:: I think ngrowForce = 1, (declared as global in this file)
    //needed for godunov/diffusion stuff
    BL_ASSERT(sdcForce == 0);
    sdcForce = new MultiFab(grids,NUM_STATE,nGrowForce);
    *sdcForce = 0.0;

    DofS.resize(n_diffusion, PArrayManage);
    for (int i = 0; i < n_diffusion; ++i){
      DofS.set(i,new MultiFab(grids,NUM_STATE,0));
      DofS[i] = 0.0;
    }

    RhoH_NULN_terms.resize(n_diffusion, PArrayManage);
    for (int i = 0; i < n_diffusion; ++i){
      RhoH_NULN_terms.set(i,new MultiFab(grids,1,0));
      // want to do this right before filling in rhoh_advection
      //      RhoH_NULN_terms[i] = 0.0;
    }
    // Allocate space for NULN terms at time n, used for advective reflux
    diffusion->allocFluxBoxesLevel(reflux_terms,0,1);

    I_R.resize(n_I, PArrayManage);
    for (int i = 0; i < n_I; ++i){
      I_R.set(i,new MultiFab(grids,NUM_STATE,0));
      I_R[i] = 0.0;
    }
    I_AD.resize(n_I, PArrayManage);
    for (int i = 0; i < n_I; ++i){
      I_AD.set(i,new MultiFab(grids,NUM_STATE,0));
      I_AD[i] = 0.0;
    }
}

// CEG:: Going to just pass Adv, Diff, and NULN fabs to 
//  CONPSOLV()
// void
// HeatTransfer::make_rxn_coeffs(int sdc_flag)
// {

//   if(sdc_flag == 0){
//     //doing provisional rxns integration
//     for (MFIter Amfi(*aofs); Amfi.isValid(); ++Amfi)
//       {
// 	const Box& bx = Amfi.validbox();
// 	FArrayBox& Afab(bx,nspecies+1);
// 	FArrayBox& Dfab(bx,nDiffus*(nspecies+1));
// 	FArrayBox& NULNfab(bx,nDiffus);

// 	//CEG:: is there a better way to do this that doesn't
// 	//   involve this copy?
// 	Afab.copy((*aofs)[Amfi],first_spec,0,nspecies);
// 	Afab.copy((*aofs)[Amfi],RhoH,nspecies,1);
// 	//load up the Diffusion fab so that it contains
// 	//  spec1, spec2,...(at time 1),spec1, spec2,...(at time 2),
// 	//  etc., RhoH_t1,RhoH_t2,...
// 	for (int j = 0; j < nDiffus; j++)
// 	  { 
// 	    Dfab.copy(DofS[j][Amfi],first_spec,j*nspecies,nspecies);
// 	    Dfab.copy(DofS[j][Amfi],RhoH,nDiffus*nspecies+j,1);
// 	    NULNfab.copy(RhoH_NULN_terms[j][Amfi],0,j,1);

// 	  }
	
// 	provis(bx.loVect(), bx.hiVect(),
// 	       Afab.dataPtr(),Dfab.dataPtr(),
// 	       NULNfab.dataPtr());
//       }

//   }
// }

// CEG:: mf = S_old
void
HeatTransfer::chem_sdc (MultiFab&  mf,
			Real       dt,
			int        sdc_flag,
			int        ngrow)
{
    const Real strt_time = ParallelDescriptor::second();
    const int ycomp     = first_spec;
    const int Tcomp     = Temp;


    //CEG:: would it have been better to just load ydot here???

    if (hack_nochem)
    {
      return;        
    }
    else
    {
        Real p_amb, dpdt_factor;
        FORT_GETPAMB(&p_amb, &dpdt_factor);
        const Real Patm = p_amb / P1atm_MKS;

        FArrayBox* chemDiag = 0;
	FArrayBox Afab;
	FArrayBox Dfab;
	FArrayBox NULNfab;

        if (do_not_use_funccount)
        {
	    MultiFab tmp;

            tmp.define(mf.boxArray(), 1, 0, mf.DistributionMap(), Fab_allocate);
	    
            for (MFIter Smfi(mf); Smfi.isValid(); ++Smfi)
            {
                FArrayBox& fb = mf[Smfi];
                const Box& bx = Smfi.validbox();
		FArrayBox& fc = tmp[Smfi];
       

		Afab.resize(bx,nspecies+1);
		Dfab.resize(bx,(nspecies+1)*n_diffusion);
		NULNfab.resize(bx,n_diffusion);
		//CEG:: is there a better way to do this that doesn't
		//   involve this copy?
		Afab.copy((*aofs)[Smfi],first_spec,0,nspecies);
		Afab.copy((*aofs)[Smfi],RhoH,nspecies,1);
		//load up the Diffusion fab so that it contains
		//  spec1, spec2,...(at time 1),
		//  spec1, spec2,...(at time 2),
		//  ..., 
		//  RhoH_t1,RhoH_t2,...
		for (int j = 0; j < n_diffusion; j++)
		{ 
		  Dfab.copy(DofS[j][Smfi],first_spec,j*nspecies,nspecies);
		  Dfab.copy(DofS[j][Smfi],RhoH,n_diffusion*nspecies+j,1);
		  NULNfab.copy(RhoH_NULN_terms[j][Smfi],0,j,1);
		}

                if (plot_reactions &&
                    BoxLib::intersect(mf.boxArray(),auxDiag["REACTIONS"]->
				      boxArray()).size() != 0)
                {
                    chemDiag = &( (*auxDiag["REACTIONS"])[Smfi] );
                }

                getChemSolve().solveTransient_sdc(fb,fb,Afab,Dfab,NULNfab,
						  fc,bx,Density,ycomp,
						  Tcomp,RhoH,
						  dt,Patm,chem_integrator,
						  chemDiag,sdc_flag);
            }
            //
            // When ngrow>0 this does NOT properly update FuncCount_Type since parallel
            // copy()s do not touch ghost cells.  We'll ignore this since we're not using
            // the FuncCount_Type anyway.
            //
	    get_new_data(FuncCount_Type).copy(tmp);
        }
        else
        {
            BoxArray ba = mf.boxArray();

            ba.maxSize(max_grid_size_chem);

            if (ba.size() < 2*ParallelDescriptor::NProcs() && max_grid_size_chem >= 16)
                //
                // Let's chop the grids up a bit more.
                // We want to try and level out the chemistry work.
                //
                ba.maxSize(max_grid_size_chem/2);

            DistributionMapping dm = getFuncCountDM(ba,ngrow);

            MultiFab tmp, fcnCntTemp;

            tmp.define(ba, mf.nComp(), 0, dm, Fab_allocate);

            fcnCntTemp.define(ba, 1, 0, dm, Fab_allocate);

            MultiFab diagTemp;
            const bool do_diag = plot_reactions && BoxLib::intersect(ba,auxDiag["REACTIONS"]->boxArray()).size() != 0;
            if (do_diag)
            {
                diagTemp.define(ba, auxDiag["REACTIONS"]->nComp(), 0, dm, Fab_allocate);
                diagTemp.copy(*auxDiag["REACTIONS"]); // Parallel copy
            }

            if (verbose && ParallelDescriptor::IOProcessor())
                std::cout << "*** strang_chem: FABs in tmp MF: " << tmp.size() << std::endl;

            tmp.copy(mf); // Parallel copy.

            for (MFIter Smfi(tmp); Smfi.isValid(); ++Smfi)
            {
                FArrayBox& fb = tmp[Smfi];
                const Box& bx = Smfi.validbox();
                FArrayBox& fc = fcnCntTemp[Smfi];
                chemDiag = (do_diag ? &(diagTemp[Smfi]) : 0);

		Afab.resize(bx,nspecies+1);
		Dfab.resize(bx,(nspecies+1)*n_diffusion);
		NULNfab.resize(bx,n_diffusion);
		//CEG:: is there a better way to do this that doesn't
		//   involve this copy?
		Afab.copy((*aofs)[Smfi],first_spec,0,nspecies);
		Afab.copy((*aofs)[Smfi],RhoH,nspecies,1);
		//load up the Diffusion fab so that it contains
		//  spec1, spec2,...(at time 1),spec1, spec2,...(at time 2),
		//  etc., RhoH_t1,RhoH_t2,...
		for (int j = 0; j < n_diffusion; j++)
		{ 
		  Dfab.copy(DofS[j][Smfi],first_spec,j*nspecies,nspecies);
		  Dfab.copy(DofS[j][Smfi],RhoH,n_diffusion*nspecies+j,1);
		  NULNfab.copy(RhoH_NULN_terms[j][Smfi],0,j,1);
		}

                getChemSolve().solveTransient_sdc(fb,fb,Afab,Dfab,NULNfab,
						  fc,bx,Density,ycomp,
						  Tcomp,RhoH,
						  dt,Patm,chem_integrator,
						  chemDiag,sdc_flag);     
	    }

            mf.copy(tmp); // Parallel copy.

            if (do_diag)
            {
                auxDiag["REACTIONS"]->copy(diagTemp); // Parallel copy
            }

            if (ngrow == 0)
            {
                //
                // Working on valid region of state.
                //
                get_new_data(FuncCount_Type).copy(fcnCntTemp); // Parallel copy.
            }
            else
            {
                //
                // Can't directly use a parallel copy to update FuncCount_Type.
                //
                MultiFab& FC = get_new_data(FuncCount_Type);

                BoxArray ba = FC.boxArray();
                ba.grow(ngrow);
                MultiFab grownFC(ba, 1, 0);
                
                for (MFIter mfi(FC); mfi.isValid(); ++mfi)
                    grownFC[mfi].copy(FC[mfi]);

                grownFC.copy(fcnCntTemp); // Parallel copy.

                for (MFIter mfi(grownFC); mfi.isValid(); ++mfi)
                    FC[mfi].copy(grownFC[mfi]);
            }
        }
    }

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;

    ParallelDescriptor::ReduceRealMax(run_time,IOProc);

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::strang_chem time: " << run_time << std::endl;
}

void
HeatTransfer::compute_edge_states_sdc (Real dt, std::vector<bool>* 
				       state_comps_to_compute)
{
    //
    // Compute edge states, store internally.  Do this to
    // avoid recomputing these, and to allow inter-equation consistency.  Note,
    // storage order in EdgeState same as in State_Type.
    // NOTE: Ordering is important here, must do rho.Y and Temp BEFORE RhoH and
    //       Density, but then it doesn't matter.
    //

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "... computing edge states\n";
    //
    // Get simulation parameters.
    //
    const Real* dx             = geom.CellSize();
    const Real  prev_time      = state[State_Type].prevTime();
    const Real  prev_pres_time = state[Press_Type].prevTime();
    //
    // NOTE: Massive memory bloat here...how many MultiFab can we waste??
    // Get viscous forcing on the ones we need, don't bother allocating others
    //
    const int nState = desc_lst[State_Type].nComp();
    PArray<MultiFab> visc_terms(nState,PArrayManage);
    const int use_forces_in_trans = godunov->useForcesInTrans();
    //
    // "do_predict" states are predicted normally, after special states
    //
    std::vector<bool> do_predict(nState,true);

    //REMOVE ME
    //CEG: use_forcces_in_trans = 0
    //     do mom diff = 0
    //     do set rho to species sum = 1

    // for sdc do velocity separately
    for (int d=0; d<BL_SPACEDIM; ++d)
      do_predict[Xvel+d] = false;
     
    for (int sigma=first_spec; sigma<=last_spec; ++sigma)
        do_predict[sigma] = false; // Will get these in a special way
    
    if (do_set_rho_to_species_sum)
    {
        do_predict[Density] = false;
        do_predict[RhoH]    = false;
    }

    //
    // This logic and the associated array passed in allows the computation
    // of the edge states to be shut off for specific components.  This is
    // intended to allow special components, such as RhoK and RhoEps in
    // TurbHT be treated differently.  This logic tries to insure that
    // all components with interdependencies are turned on at the same time.
    //
    // CEG: if i use this comp-edge-states, then need to change this logic
    // so Temp doesn't need to get recomputed too.
    std::vector<bool> compute_comp(nState, true);

    if (state_comps_to_compute != 0)
    {
        BL_ASSERT(state_comps_to_compute->size() == nState);

        for (int cmp = 0; cmp < nState; cmp++)
            compute_comp[cmp] = (*state_comps_to_compute)[cmp];

	// for now assume state_comps_to_compute is made sensibly
//         if (compute_comp[Density] || compute_comp[Temp] ||
//             compute_comp[RhoH]    || compute_comp[first_spec])
//         {
//             BL_ASSERT(compute_comp[Density]);
//             BL_ASSERT(compute_comp[Temp]);
//             BL_ASSERT(compute_comp[RhoH]);

//             for (int sigma=first_spec; sigma<=last_spec; ++sigma)
//                 BL_ASSERT(compute_comp[sigma]);
//         }
    }

    const int nGrowF = 1;

    MultiFab* divu_fp = create_mac_rhs_grown(nGrowF,prev_time,dt);

    // Get all the normal visc terms for everything but velocity
    // CEG:: this gets visc terms for 13-15
    for (int sigma=BL_SPACEDIM; sigma<nState; ++sigma)
    {
        if (do_predict[sigma] && compute_comp[sigma])
        {
            BL_ASSERT( sigma < first_spec || sigma > last_spec );
            visc_terms.set(sigma, new MultiFab(grids,1,nGrowF));
            if (be_cn_theta == 1.0)
            {
                visc_terms[sigma].setVal(0.0,0,1,nGrowF);
            }
            else
            {
                getViscTerms(visc_terms[sigma],sigma,1,prev_time);
            }
        }
    }
    //
    // Loop on grids, and compute edge fluxes
    //
    FArrayBox edge[BL_SPACEDIM],Rho,U,state;
    //
    // FillPatch'd state data.
    //
    for (FillPatchIterator S_fpi(*this,*divu_fp,Godunov::hypgrow(),prev_time,State_Type,
				 0,nState); S_fpi.isValid(); ++S_fpi)
    {
        //
        // Gonna need this array on a per-grid basis
        //
        std::vector<bool> this_edge_state_computed(nState,false);

        const int i = S_fpi.index();

        FArrayBox tforces, tvelforces;

        Rho.resize(S_fpi().box(),1);
        U.resize(S_fpi().box(),BL_SPACEDIM);

        Rho.copy(S_fpi(),Density,0,1);
        U.copy(S_fpi(),Xvel,0,BL_SPACEDIM);
        //
        // Set up the workspace for the godunov Box (also resize "edge" 
	// for later)
        //
        Array<int> u_bc[BL_SPACEDIM];
        D_TERM(u_bc[0] = getBCArray(State_Type,i,0,1);,
               u_bc[1] = getBCArray(State_Type,i,1,1);,
               u_bc[2] = getBCArray(State_Type,i,2,1);)

        godunov->Setup(grids[i], dx, dt, 0,
                       edge[0], u_bc[0].dataPtr(),
                       edge[1], u_bc[1].dataPtr(),
#if (BL_SPACEDIM == 3)
                       edge[2], u_bc[2].dataPtr(),
#endif
                       U, Rho, tvelforces);

        const int velpred = 0; // Already have edge velocities for 
	                       // transverse derivative

        tvelforces.clear();

/////////////////////////////////////////////////////////////////////////
// SDC stuff of interest
/////////////////////////////////////////////////////////////////////////

        //
        // Get spec edge states
	if (compute_comp[first_spec])
	{
	  // resizes tforces to length = nspecies 
	  NavierStokes::getForce(tforces,i,nGrowF,first_spec,nspecies,
#ifdef GENGETFORCE
				 prev_time,
#endif		 
				 Rho);
//      std::ofstream edge_fab("tforces");
//      tforces.writeOn(edge_fab);
//      edge_fab.close();
	  
	  for (int comp = first_spec; comp <= last_spec; comp++)
	  {
	    int use_conserv_diff = 
	      (advectionType[comp] == Conservative) ? true : false;
	    Array<int> bc = getBCArray(State_Type,i,comp,1);

	    int fcomp = comp - first_spec;

	    // sums the external forces (such as gravity) with SDC forcing terms
	    // CEG:: FIX ME?  don't use the state data, potentially use
	    //   rho if not conservative.  
	    Sum_extF_sdcF(S_fpi(), comp, tforces, fcomp, 1,
				   (*sdcForce)[i], comp,
				   Rho, use_conserv_diff);

	    //  Assume sdcForces assembled outside of compute_edge_states()
	    godunov->edge_states(grids[i], dx, dt, velpred,
				 u_mac[0][i], edge[0],
				 u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
				 u_mac[2][i], edge[2],
#endif
				 U,S_fpi(),(*sdcForce)[i],
				 (*divu_fp)[i],
				 comp,comp,bc.dataPtr(), 
				 use_conserv_diff,FPU);

	    for (int d=0; d<BL_SPACEDIM; ++d)
	      (*EdgeState[d])[i].copy(edge[d],0,comp,1);

	    this_edge_state_computed[comp] = true;
	  }		
	}
        //
        // set density edge states = sum(rho*Y_i)
        //
        if (compute_comp[Density])
        {
            if (do_set_rho_to_species_sum)
            {

                for (int d=0; d<BL_SPACEDIM; ++d)
                {
                    (*EdgeState[d])[i].setVal(0.0,edge[d].box(),Density,1);
                    for (int sigma=first_spec; sigma<=last_spec; ++sigma)
                        (*EdgeState[d])[i].plus((*EdgeState[d])[i],
                                                edge[d].box(), sigma,Density,1);
                }
                this_edge_state_computed[Density] = true;
            }
            else
            {
                BoxLib::Error("No code yet for rho != sum(rho.Y)");
            }

            
        }

        if (compute_comp[Temp])
        {
            //
            // Get Temp edge states via extrap.
            //
            const int comp = 0;
            const int state_ind = Temp;
            int use_conserv_diff = 
                      (advectionType[state_ind] == Conservative) ? true : false;
            state.resize(S_fpi().box(),1);
            state.copy(S_fpi(),state_ind,0,1);

            NavierStokes::getForce(tforces,i,nGrowF,state_ind,1,
#ifdef GENGETFORCE
				   prev_time,
#endif		 

				   Rho);
            Array<int> bc = getBCArray(State_Type,i,state_ind,1);

            AdvectionScheme adv_scheme = FPU;

            if (adv_scheme == PRE_MAC)
            {
                godunov->Sum_tf_divu_visc(state, tforces,  comp, 1,
                                          visc_terms[state_ind][i], 0,
                                          (*divu_fp)[i], Rho, use_conserv_diff);

                int iconserv_dummy = 0;
                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                     u_mac[2][i], edge[2],
#endif
                                     U, state, tforces, (*divu_fp)[i],
                                     comp, state_ind, bc.dataPtr(),
                                     iconserv_dummy, PRE_MAC);

            }
            else
            {

                FArrayBox junkDivu(tforces.box(),1);
                junkDivu.setVal(0.);
                godunov->Sum_tf_divu_visc(state, tforces,  comp, 1,
                                          visc_terms[state_ind][i], 0,
                                          junkDivu, Rho, use_conserv_diff);

                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                     u_mac[2][i], edge[2],
#endif
                                     U, state, tforces, (*divu_fp)[i],
                                     comp, state_ind, bc.dataPtr(), 
                                     use_conserv_diff, FPU);
            }

            for (int d=0; d<BL_SPACEDIM; ++d)
                (*EdgeState[d])[i].copy(edge[d],0,state_ind,1);

            this_edge_state_computed[state_ind] = true;
        }

        if (compute_comp[RhoH])
        {
            //
            // Set rhoh on edges = sum(rho.Y.H)
            //
            FArrayBox h;
            for (int d=0; d<BL_SPACEDIM; ++d)
            {
                (*EdgeState[d])[i].setVal(0.0,edge[d].box(),RhoH,1);
                h.resize(edge[d].box(),nspecies);
                getChemSolve().getHGivenT(h,(*EdgeState[d])[i],
                                          edge[d].box(),Temp,0);
                h.mult((*EdgeState[d])[i],edge[d].box(),first_spec,0,
                       nspecies);
                
                (*EdgeState[d])[i].setVal(0.0,edge[d].box(),RhoH,1);
                for (int comp=0; comp<nspecies; ++comp)
                    (*EdgeState[d])[i].plus(h,edge[d].box(),comp,RhoH,1);
            }
            this_edge_state_computed[RhoH] = true;
        }

//////////////////////////////////////////////////////////////////
// End of stuff of interest
//////////////////////////////////////////////////////////////////

        //
        // Now do the rest as normal
        //
        state.resize(S_fpi().box(),1);
        for (int state_ind=0; state_ind<nState; ++state_ind)
        {
            if (do_predict[state_ind]                &&
                !this_edge_state_computed[state_ind] &&
                compute_comp[state_ind])
            {
                int use_conserv_diff =
                    (advectionType[state_ind] == Conservative) ? true : false;
                //
                // Do it the old-fashioned way.
                //
                state.copy(S_fpi(),state_ind,0,1);
                const int comp = 0;
                NavierStokes::getForce(tforces,i,nGrowF,state_ind,1,
#ifdef GENGETFORCE
				       prev_time,
#endif		 
				       Rho);
                godunov->Sum_tf_divu_visc(state, tforces, comp, 1,
                                          visc_terms[state_ind][i], 0,
                                          (*divu_fp)[i], Rho,
                                          use_conserv_diff);
                Array<int> bc = getBCArray(State_Type,i,state_ind,1);
                int iconserv_dummy = 0;
                godunov->edge_states(grids[i], dx, dt, velpred,
                                     u_mac[0][i], edge[0],
                                     u_mac[1][i], edge[1],
#if (BL_SPACEDIM==3)
                                     u_mac[2][i], edge[2],
#endif
                                     U,state,tforces,(*divu_fp)[i],
                                     comp,state_ind,bc.dataPtr(),
                                     iconserv_dummy,PRE_MAC);

                for (int d=0; d<BL_SPACEDIM; ++d)
                    (*EdgeState[d])[i].copy(edge[d],0,state_ind,1);

                this_edge_state_computed[state_ind] = true;
            }
        }

    }


    delete divu_fp;
}


void
HeatTransfer::differential_spec_diffusion_update_sdc (Real dt,
						      int  corrector)
{
    const Real strt_time = ParallelDescriptor::second();

    if (hack_nospecdiff)
    {
        if (ParallelDescriptor::IOProcessor())
            std::cout << "... HACK!!! skipping spec diffusion " << std::endl;

        if (!corrector)
            MultiFab::Copy(get_new_data(State_Type),get_old_data(State_Type),first_spec,first_spec,nspecies,0);

        for (int d = 0; d < BL_SPACEDIM; ++d)
        {
            SpecDiffusionFluxn[d]->setVal(0,0,nspecies);
            SpecDiffusionFluxnp1[d]->setVal(0,0,nspecies);
        }
        for (int comp = 0; comp < nspecies; ++comp)
            spec_diffusion_flux_computed[comp] = HT_Diffusion;

        return;
    }


    //
    // Build single component edge-centered array of MultiFabs for fluxes
    //
    MultiFab** fluxSCn;
    MultiFab** fluxSCnp1;
    const int nGrow   = 0;
    const int nCompSC = 1;
    const int sCompSC = 0;
    diffusion->allocFluxBoxesLevel(fluxSCn,  nGrow,nCompSC);
    diffusion->allocFluxBoxesLevel(fluxSCnp1,nGrow,nCompSC);
    //
    // Set diffusion solve mode
    //
    Diffusion::SolveMode solve_mode = Diffusion::PREDICTOR;
    //
    // Do diffusion solve for each scalar...but dont reflux.
    // Save the fluxes, coeffs and source term, we need 'em for later
    //

    MultiFab* alpha = 0; // Allocate lazily
    MultiFab delta_rhs(grids, nspecies, nGrow);
    MultiFab **betan, **betanp1;
    diffusion->allocFluxBoxesLevel(betan  ,nGrow,nspecies);
    diffusion->allocFluxBoxesLevel(betanp1,nGrow,nspecies);
    Array<int> rho_flag(nspecies,0);
    MultiFab *alphaSC, *delta_rhsSC, **betanSC, **betanp1SC;
    const MultiFab* Rh = get_rho_half_time();
    // FIXME debugging
    //    const Real species_diffusion_theta = 1.0;
    const Real species_diffusion_theta = 0.5;

    for (int sigma = 0; sigma < nspecies; ++sigma)
    {
	const int state_ind = first_spec + sigma;

	// set rho_flag = 2 (diffusion type = laplacian (S/rho))
	// declare alpha, delta_rhs = zero
	// beta_n = D_m^n
	// beta_np1 = D_m^n+1
	diffuse_scalar_setup(dt, state_ind, &rho_flag[sigma], delta_rhsSC,
			     alphaSC, betanSC, betanp1SC);
	
	if (state_ind == first_spec && alphaSC)
	{
	    alpha = new MultiFab(grids, nspecies, nGrow);
	}
	else
	{
	    if ((!alphaSC) ^ !alpha)
		BoxLib::Error("All diff-diffusion must be of same form");
	}   
	//    
	// Nab a copy of the coeffs, call diffuser
	//
	if (alphaSC)
	    MultiFab::Copy(*alpha,*alphaSC,sCompSC,sigma,nCompSC,nGrow);
	
	for (int d=0; d<BL_SPACEDIM; ++d)
	{
	    if (betanSC)
		MultiFab::Copy(  *betan[d],  *betanSC[d],sCompSC,sigma,nCompSC,nGrow);
	    if (betanp1SC)
		MultiFab::Copy(*betanp1[d],*betanp1SC[d],sCompSC,sigma,nCompSC,nGrow);
	}
	//
	// Make sure we've got a place for delta_rhs...predictor will dump any
	// explicit updates taken before this routine into delta_rhs for later.
	//
	MultiFab::Copy(delta_rhs,*sdcForce,state_ind,sigma,nCompSC,nGrow);

	//
	// Clean up single-component stuff, then diffuse the scalar
	//
	diffuse_cleanup(delta_rhsSC, betanSC, betanp1SC, alphaSC);
   

	//This code doesn't work
//     MultiFab* alpha = 0; 
//     MultiFab delta_rhs(grids, nspecies, nGrow);
//     MultiFab **betan, **betanp1;
//     Array<int> rho_flag(nspecies,0);
//     //    MultiFab *alphaSC, *delta_rhsSC, **betanSC, **betanp1SC;
//     const MultiFab* Rh = get_rho_half_time();
//     const Real species_diffusion_theta = 1.0;


//     diffusion->allocFluxBoxesLevel(betan  ,nGrow,nspecies);
//     diffusion->allocFluxBoxesLevel(betanp1,nGrow,nspecies);
//     alpha = new MultiFab(grids, nspecies, nGrow);
//     MultiFab::Copy(delta_rhs,(*sdcForce),first_spec,0,nspecies,0);

//     getDiffusivity(betan, prev_time, first_spec, 0, nspecies);
//     getDiffusivity(betanp1, prev_time+dt, first_spec, 0, nspecies);


//     // predictor will dump any
//     // explicit updates taken before this routine into delta_rhs for later.

//     for (int sigma = 0; sigma < nspecies; ++sigma)
//     {
//       std::cout<<"sigma "<<sigma<<std::endl;
// 	const int state_ind = first_spec + sigma;

// 	// set rho_flag = 2 (diffusion type = laplacian (S/rho))
// 	// declare alpha, delta_rhs = zero
// 	// beta_n = D_m^n
// 	// beta_np1 = D_m^n+1
// 	//diffuse_scalar_setup(dt, state_ind, &rho_flag[sigma], delta_rhsSC,
// 	//		     alphaSC, betanSC, betanp1SC);

// 	rho_flag[sigma] = 2;
// 	*alpha = 0.0;
// 	**betan = 0.0;
// 	**betanp1 = 0.0;


// 	if (state_ind == 4)
// 	  {
// 	std::cout<<"dt = "<<dt<<std::endl;
// 	std::cout<<"rho_flag = "<<rho_flag[sigma]<<std::endl;
// 	VisMF::Write(delta_rhs,"delta_sdc");
// 	VisMF::Write(*betan[0],"betaxn_sdc");
// 	VisMF::Write(*betan[1],"betayn_sdc");
// 	VisMF::Write(*betanp1[0],"betaxnp1_sdc");
// 	VisMF::Write(*betanp1[1],"betaynp1_sdc");
// 	// not built yet
// 	//	VisMF::Write(*alpha,"alpha_sdc");

// 	  }

	diffusion->diffuse_scalar(dt,state_ind,species_diffusion_theta,Rh,
				  rho_flag[sigma],
                                  fluxSCn,fluxSCnp1,sigma,&delta_rhs,alpha,
                                  betan,betanp1,solve_mode);
	//
	// Pull fluxes into flux array
	//
	for (int d = 0; d < BL_SPACEDIM; ++d)
        {
	    MultiFab::Copy(*SpecDiffusionFluxn[d],  *fluxSCn[d],  sCompSC,sigma,nCompSC,nGrow);
	    MultiFab::Copy(*SpecDiffusionFluxnp1[d],*fluxSCnp1[d],sCompSC,sigma,nCompSC,nGrow);
        }
	spec_diffusion_flux_computed[sigma] = HT_Diffusion;
    }
    diffusion->removeFluxBoxesLevel(fluxSCn);
    diffusion->removeFluxBoxesLevel(fluxSCnp1);

//        VisMF::Write(*SpecDiffusionFluxnp1[0],"specfluxx_sdc");
//        VisMF::Write(*SpecDiffusionFluxnp1[1],"specfluxy_sdc");
//        VisMF::Write(*SpecDiffusionFluxn[0],"specfluxxn_sdc");
//        VisMF::Write(*SpecDiffusionFluxn[1],"specfluxyn_sdc");

    //
    // Modify update/fluxes to preserve flux sum = 0, compute new update and
    // leave modified fluxes in level data.  Do this in two stages, first for
    // the explicit fluxes, then the implicit ones (so send in rhs=0 for the
    // second one...).
    //
    const int  dataComp  = 0;
     const Real prev_time = state[State_Type].prevTime();
     const Real cur_time  = state[State_Type].curTime();

     // FIXME:: should have done this earlier in getViscTerms but then need to get
     // snew = sold +dt*delta_rhs + dt*Diff_n   somehow
     // (adv term got shoved into delta_rhs)
     adjust_spec_diffusion_update(get_new_data(State_Type),&get_old_data(State_Type),
				  first_spec,dt,prev_time,rho_flag,Rh,dataComp,
                                  &delta_rhs,alpha,betan);

    diffusion->removeFluxBoxesLevel(betan);
    delta_rhs.clear();

    adjust_spec_diffusion_update(get_new_data(State_Type),&get_new_data(State_Type),
				 first_spec,dt,cur_time,rho_flag,Rh,dataComp,0,
                                 alpha,betanp1);

    diffusion->removeFluxBoxesLevel(betanp1);
    if (alpha)
	delete alpha;
    //
    // Now do reflux with new, improved fluxes
    //
    if (do_reflux && corrector)
    {
        {
            FArrayBox fluxtot;
            for (int d = 0; d < BL_SPACEDIM; d++)
            {
                for (MFIter fmfi(*SpecDiffusionFluxn[d]); fmfi.isValid(); ++fmfi)
                {
                    const Box& ebox = (*SpecDiffusionFluxn[d])[fmfi].box();

                    fluxtot.resize(ebox,nspecies);
                    fluxtot.copy((*SpecDiffusionFluxn[d])[fmfi], ebox,0,ebox,0,nspecies);
                    fluxtot.plus((*SpecDiffusionFluxnp1[d])[fmfi],ebox,0,0,nspecies);

                    if (level < parent->finestLevel())
                        getLevel(level+1).getViscFluxReg().CrseInit(fluxtot,ebox,d,0,first_spec,nspecies,-dt);

                    if (level > 0)
                        getViscFluxReg().FineAdd(fluxtot,d,fmfi.index(),0,first_spec,nspecies,dt);
                }
            }
        }
	if (level < parent->finestLevel())
	    getLevel(level+1).getViscFluxReg().CrseInitFinish();
    }

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;

    ParallelDescriptor::ReduceRealMax(run_time,IOProc);

    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::differential_spec_diffusion_update(): time: " << run_time << std::endl;
}

void HeatTransfer::advance_cleanup_sdc (Real dt,
					int  iteration,
					int  ncycle)
{
     if (level == parent->finestLevel())
    {
        delete [] u_mac;
        u_mac = 0;
    }

    delete aofs;
    aofs = 0;

    // SDC stuff
    delete sdcForce;
    sdcForce = 0;

    DofS.clear();
    I_R.clear();
    I_AD.clear();
    RhoH_NULN_terms.clear();
    diffusion->removeFluxBoxesLevel(reflux_terms);


}


// CEG:: don't think i'll be using this function
void
HeatTransfer::getForce_sdc (int  scomp,
			    int  ncomp,
			    int  ngrow)
{
  //sdcForce got appropriately sized in advance_setup()
  //    force.resize(BoxLib::grow(grids[gridno],ngrow),ncomp);

  const Real  grav  = gravity;
  FArrayBox   Rho;

  const Real  prev_time  = state[State_Type].prevTime();
  const int nState = desc_lst[State_Type].nComp();

  for (FillPatchIterator S_fpi(*this,*sdcForce,nGrowForce,prev_time,
			       State_Type,0,nState); 
       S_fpi.isValid(); ++S_fpi)
  {
    const int i = S_fpi.index();

    Rho.resize(S_fpi().box(),1);
    Rho.copy(S_fpi(),Density,0,1);

    BL_ASSERT(Rho.box().contains((*sdcForce)[i].box()));

    RealBox     gridloc = RealBox(grids[i],geom.CellSize(),geom.ProbLo());
    const Real* dx      = geom.CellSize();
    const int*  s_lo    = Rho.loVect();
    const int*  s_hi    = Rho.hiVect();

    //std::cout<<"inside getForce_sdc...\n";
    //CEG:: don't forget about gravity?
    // i think everything for velocity is done separately, even edge state
    // stuff, so maybe can only worry about scalars for now.
    //
    // Inside BoxLib/ArrayLim.H have
    // #define ARLIM(x)  x[0],x[1]
    FORT_MAKEFORCE_SDC (&prev_time,
			(*sdcForce)[i].dataPtr(scomp),
			Rho.dataPtr(),
			ARLIM(s_lo), ARLIM(s_hi),
			dx,
			gridloc.lo(),
			gridloc.hi(),
			&grav,&ncomp);
  }
}

//
//CEG:: The current getViscTerms would probably work for this as long as 
// it was changed to accept load_comp as a parameter
//
void
HeatTransfer::getViscTerms_sdc (MultiFab& visc_terms,
                            int       src_comp, 
                            int       num_comp,
                            Real      time)
{
    BL_ASSERT(visc_terms.boxArray()==grids);
    //
    // Load "viscous" terms, starting from component = 0.
    //
    // JFG: for species, this procedure returns the *negative* of the 
    // divergence of 
    // of the diffusive fluxes.  specifically, in the mixture averaged case, the
    // diffusive flux vector for species k is
    //
    //       j_k = - rho D_k,mix grad Y_k
    //
    // so the divergence of the flux, div dot j_k, has a negative in it.  
    // instead 
    // this procedure returns - div dot j_k to remove the negative.
    //
    // note the fluxes used in the code are extensive, that is, scaled 
    // by the areas
    // of the cell edges.  the calculation of the divergence is the sum 
    // of un-divided
    // differences of the extensive fluxes, all divided by volume.  so 
    // the effect is 
    // to give the true divided difference approximation to the divergence
    // of the intensive flux.
    //
    const int  last_comp = src_comp + num_comp - 1;
    const int  nGrow     = visc_terms.nGrow();

 
    // this function is only meant for the species
    //looks like can just call getRhoHViscTerms() from advance()
    for (int icomp = src_comp; icomp <= last_comp; icomp++)
      BL_ASSERT((icomp >= first_spec && icomp <= last_spec));//|| icomp == RhoH);
    
//     if (src_comp == first_spec)
//     {
      // CEG: as far as I can tell, this just gets the diffusion 
      // terms for the species (it computes the fluxes first, adjusts
      // to conserve mass by subracting from dominate species, and
      // then takes the divergence)
      //
      // Assume visc_terms has same structure as State MF
      compute_differential_diffusion_terms(visc_terms,src_comp,time);
      //    VisMF::Write(visc_terms,"sdc_diff");
//     }
//     else // can only be RhoH
//       {

 
//       const int rho_flag  = 1;

//       MultiFab** beta;
//       diffusion->allocFluxBoxesLevel(beta);
//       //
//       // + div lambda grad T + Q
//       //
//       getDiffusivity(beta, time, Temp, 0, 1);

//     //    VisMF::Write((*beta[0]),"beta_orig");
//     //VisMF::Write((*beta[1]),"betay_orig");

//     diffusion->getViscTerms(visc_terms,src_comp,Temp,time,rho_flag,0,beta);
//     diffusion->removeFluxBoxesLevel(beta);
//    }

    //
    // Ensure consistent grow cells
    //    
    if (nGrow > 0)
    {
        for (MFIter mfi(visc_terms); mfi.isValid(); ++mfi)
        {
            FArrayBox& vt  = visc_terms[mfi];
            const Box& box = mfi.validbox();
           
	    // Does a 0th order extrapolation (ie just takes the value from 
	    //   nearest valid cell) to fill ghost cells
	    FORT_VISCEXTRAP(vt.dataPtr(src_comp),ARLIM(vt.loVect()),ARLIM(vt.hiVect()),
                            box.loVect(),box.hiVect(),&num_comp);
        }
        visc_terms.FillBoundary(src_comp,num_comp);
        //
        // Note: this is a special periodic fill in that we want to
        // preserve the extrapolated grow values when periodic --
        // usually we preserve only valid data.  The scheme relies on
        // the fact that there is good data in the "non-periodic" grow cells.
        // ("good" data produced via VISCEXTRAP above)
        //
        geom.FillPeriodicBoundary(visc_terms,src_comp,num_comp,true);
    }
}


// CEG:: don't think i'm going to be using this function
void
HeatTransfer::scalar_advection_update_sdc (Real dt,
					   int  scomp,
					   int  lcomp,
                                       MultiFab *forces)
{
    MultiFab&  S_old     = get_old_data(State_Type);
    MultiFab&  S_new     = get_new_data(State_Type);
    MultiFab&  Aofs      = *aofs;
    const Real prev_time = state[State_Type].prevTime();
    Array<int> state_bc;
    //
    // Compute inviscid estimate of scalars.
    // (do rho separate, as we do not have rho at new time yet)
    //

    //CEG:: Need to put in logic to assure right components
    // right now assuming that pass in first_spec to last_spec
    for (int comp = scomp; comp <= lcomp; )
    {
      if (comp == first_spec) 
      {
	for (MFIter f_mfi(*forces); f_mfi.isValid(); ++f_mfi)
	{       
	  const int i = f_mfi.index();
	  
	  // sets S_new = S_old - dt*aofs + dt*forces
	  godunov->Add_aofs_tf(S_old[i],S_new[i],first_spec,
			       nspecies,Aofs[i],first_spec,(*forces)[i],
			       first_spec,grids[i],dt);

	}
	
	comp += nspecies;
      }
      else
	// right now not meant for anything else
	BoxLib::Error("scalar_advection_update_sdc() only meant for species");
    }
    
    //
    // Call ScalMinMax to avoid overshoots in the scalars.
    //
    if ( do_scalminmax && (scomp <= lcomp) )
    {
        const int num_scalars = lcomp - Density + 1;
        //
        // Must do FillPatch here instead of MF iterator because we need the
        // boundary values in the old data (especially at inflow).
        //
        for (FillPatchIterator S_fpi(*this,S_old,1,prev_time,State_Type,Density,num_scalars);
             S_fpi.isValid();
             ++S_fpi)
        {
            const int i = S_fpi.index();
            for (int sigma = scomp; sigma <= lcomp; sigma++)
            {
                const int index_new_s   = sigma;
                const int index_new_rho = Density;
                const int index_old_s   = index_new_s   - Density;
                const int index_old_rho = index_new_rho - Density;
                state_bc = getBCArray(State_Type,i,sigma,1);
                if (advectionType[sigma] == Conservative)
                {
                    godunov->ConservativeScalMinMax(S_fpi(),S_new[S_fpi],
                                                    index_old_s, index_old_rho,
                                                    index_new_s, index_new_rho,
                                                    state_bc.dataPtr(),grids[i]);
                }
                else if (advectionType[sigma] == NonConservative)
                {
                    godunov->ConvectiveScalMinMax(S_fpi(),S_new[S_fpi],index_old_s,sigma,
                                                  state_bc.dataPtr(),grids[i]);
                }
            }
        }
    }
}

void
HeatTransfer::diffuse_spec_setup_sdc (int        istate,
				      Real       time,
				      Real       dt, 
				      MultiFab*& delta_rhs)
{
    //
    // Chemistry split, no source terms
    //
    delta_rhs = new MultiFab(grids,1,0);
    delta_rhs->setVal(0);
}

//
// For use in compute_edge_states
// replacing a function from Godunove.cpp
//
void
HeatTransfer::Sum_extF_sdcF (FArrayBox& S,
			     int        s_ind,
			     FArrayBox& extF,
			     int        extf_ind,
			     int        num_comp,
			     FArrayBox& sdcF,
			     int        sdcf_ind,
			     FArrayBox& rho,
			     int        isconserv)
{
    BL_ASSERT(S.nComp()    >= s_ind+num_comp);
    BL_ASSERT(extF.nComp() >= extf_ind+num_comp);
    BL_ASSERT(sdcF.nComp() >= s_ind+num_comp);
    BL_ASSERT(rho.nComp()  == 1             );
    BL_ASSERT(isconserv    == 1             );
    
    const int *slo    = S.loVect();
    const int *shi    = S.hiVect();
    const int *eflo    = extF.loVect();
    const int *efhi    = extF.hiVect();
    const int *sflo    = sdcF.loVect();
    const int *sfhi    = sdcF.hiVect();
    const int *rlo    = rho.loVect();
    const int *rhi    = rho.hiVect();
    const Real *Sdat  = S.dataPtr(s_ind);
    const Real *EFdat = extF.dataPtr(extf_ind);
    const Real *SFdat = sdcF.dataPtr(sdcf_ind);
    const Real *RHdat = rho.dataPtr();
     

    FORT_SUM_EXTF_SDCF(Sdat,  ARLIM(slo), ARLIM(shi),
		       EFdat, ARLIM(eflo), ARLIM(efhi),
		       SFdat, ARLIM(sflo), ARLIM(sfhi),
		       RHdat, ARLIM(rlo), ARLIM(rhi),
		       eflo, efhi, &num_comp, &isconserv);
}

// for lobatto
// Is really I_AD/dt with Adv term canceled with Adv term from 
//    s_a sdc update
void
HeatTransfer::make_I_AD ()
{
   
//     for (MFIter f_mfi(*I_AD[0]); f_mfi.isValid(); ++f_mfi)
//     {       
//       const int i = f_mfi.index();

//     }  

    Real half = 1.0/2.0;
    
    // for species:  I_AD = (D(t^n) + D(t^n+1))/2    
    MultiFab::Copy(I_AD[0],DofS[0],first_spec,first_spec,nspecies,0);
    MultiFab::Add(I_AD[0],DofS[2],first_spec,first_spec,nspecies,0);
    I_AD[0].mult(half,first_spec,nspecies);

    // for rhoh:  I_AD = (D(t^n) + D(t^n+1))/2
    //                  + (NULN_terms(t^n+1) + NULN_terms(t^n))/2  
    MultiFab::Copy(I_AD[0],DofS[0],RhoH,RhoH,1,0);
    MultiFab::Add (I_AD[0],DofS[2],RhoH,RhoH,1,0);
    // computed -NULN, so need to subtract here
    MultiFab::Subtract(I_AD[0],RhoH_NULN_terms[0],0,RhoH,1,0);
    MultiFab::Subtract(I_AD[0],RhoH_NULN_terms[2],0,RhoH,1,0);
    I_AD[0].mult(half,RhoH,1);
}

// void
// HeatTransfer::make_I_R_provis (Real dt)
// {
//   MultiFab& S_new = get_new_data(State_Type);
//   MultiFab& S_old = get_old_data(State_Type);
//   MultiFab tmp;

//   //CEG:: i'm probably better off to use an MFIter and a fn in 2D.F
//   // can crib from stuff in chem_sdc

//   //I_R = s_new - s_old - dt*[A + D(1) + NULN(1)]

//   tmp.define(grids,nspecies+1,0);

//   MultiFab::Copy(tmp,*aofs,first_spec,0,nspecies,0);
//   MultiFab::Copy(tmp,*aofs,RhoH,nspecies,1,0);

//   MultiFab::Add(tmp,DofS[1],first_spec,0,nspecies,0);
//   MultiFab::Add(tmp,DofS[1],RhoH,nspecies,1,0);
//   MultiFab::Add(tmp,RhoH_NULN_terms[1],0,nspecies,1,0);

//   tmp.mult(dt,0,nspecies+1);

//   MultiFab::Copy(I_R[0],S_new,first_spec,first_spec,nspecies,0);
//   MultiFab::Copy(I_R[0],S_new,RhoH,RhoH,1,0);

//   MultiFab::Subtract(I_R[0],S_old,first_spec,first_spec,nspecies,0);
//   MultiFab::Subtract(I_R[0],S_old,RhoH,RhoH,1,0);
     
//   MultiFab::Subtract(I_R[0],tmp,0,first_spec,nspecies,0);
//   MultiFab::Subtract(I_R[0],tmp,nspecies,RhoH,1,0);

// }

// void
// HeatTransfer::make_I_R_sdc (Real dt)
// {
//   MultiFab& S_new = get_new_data(State_Type);
//   MultiFab& S_old = get_old_data(State_Type);

//   // I_R = s_new - s_old - dt*[A + D(1) + (D(0)-D(2))/2
//   //                          + NULN(1) + (NULN(0)-NULN(2))/2]      
  
//   FArrayBox Afab;
//   FArrayBox Dfab;
//   FArrayBox NULNfab;

//   MultiFab tmp;

//   tmp.define(mf.boxArray(), 1, 0, mf.DistributionMap(), Fab_allocate);
  
//   for (MFIter Smfi(S_new); Smfi.isValid(); ++Smfi)
//     {
//       FArrayBox& fb = S_new[Smfi];
//       const Box& bx = Smfi.validbox();
//       FArrayBox& fc = tmp[Smfi];
       

//       Afab.resize(bx,nspecies+1);
//       Dfab.resize(bx,(nspecies+1)*n_diffusion);
//       NULNfab.resize(bx,n_diffusion);
//       //CEG:: is there a better way to do this that doesn't
//       //   involve this copy?
//       Afab.copy((*aofs)[Smfi],first_spec,0,nspecies);
//       Afab.copy((*aofs)[Smfi],RhoH,nspecies,1);
//       //load up the Diffusion fab so that it contains
//       //  spec1, spec2,...(at time 1),
//       //  spec1, spec2,...(at time 2),
//       //  ..., 
//       //  RhoH_t1,RhoH_t2,...
//       for (int j = 0; j < n_diffusion; j++)
// 	{ 
// 	  Dfab.copy(DofS[j][Smfi],first_spec,j*nspecies,nspecies);
// 	  Dfab.copy(DofS[j][Smfi],RhoH,n_diffusion*nspecies+j,1);
// 	  NULNfab.copy(RhoH_NULN_terms[j][Smfi],0,j,1);
// 	}
      
//       //look at solveTransient_sdc
//       FORT_MAKE_IR_SDC(fb,fb,Afab,Dfab,NULNfab,
// 		       fc,bx,Density,ycomp,
// 		       Tcomp,RhoH,
// 		       dt,sdc_flag);
//     }

// }

void
HeatTransfer::make_advection_sdcForce ( int       src_comp, 
					int       num_comp)
{
    MultiFab::Copy(*sdcForce,I_AD[0],src_comp,src_comp,num_comp,0);
    //MultiFab::Add(*sdcForce,I_R[0],src_comp,src_comp,num_comp,0);

    //FIXME  Debugging
    MultiFab::Copy(*sdcForce,DofS[0],first_spec,first_spec,nspecies,0);

    

    for (MFIter mfi(*sdcForce); mfi.isValid(); ++mfi)
    {
      FArrayBox& sdcF  = (*sdcForce)[mfi];
      const Box& box = mfi.validbox();
      
      // Does a 0th order extrapolation (ie just takes the value from 
      //   nearest valid cell) to fill ghost cells
      FORT_VISCEXTRAP(sdcF.dataPtr(src_comp),ARLIM(sdcF.loVect()),ARLIM(sdcF.hiVect()),
		      box.loVect(),box.hiVect(),&num_comp);
    }
    
    (*sdcForce).FillBoundary(src_comp,num_comp);
    //
    // Note: this is a special periodic fill in that we want to
    // preserve the extrapolated grow values when periodic --
    // usually we preserve only valid data.  The scheme relies on
    // the fact that there is good data in the "non-periodic" grow cells.
    // ("good" data produced via VISCEXTRAP above)
    //
    geom.FillPeriodicBoundary(*sdcForce,src_comp,num_comp,true);
}

void
HeatTransfer::make_diffusion_sdcForce ( int       src_comp, 
					int       num_comp)
{
    // might not need this copy, sdcForces should already conatin this
    MultiFab::Copy(*sdcForce,I_AD[0],src_comp,src_comp,num_comp,0);
    //    MultiFab::Add(*sdcForce,I_R[0],src_comp,src_comp,num_comp,0);
    MultiFab::Subtract(*sdcForce,DofS[1],src_comp,src_comp,num_comp,0);

    //FIXME debugging
    MultiFab::Copy(*sdcForce,RhoH_NULN_terms[0],0,RhoH,1,0);      
    MultiFab::Add(*sdcForce,RhoH_NULN_terms[1],0,RhoH,1,0);
    (*sdcForce).mult(-0.5,RhoH,1);

    for (MFIter mfi(*sdcForce); mfi.isValid(); ++mfi)
    {
      FArrayBox& sdcF  = (*sdcForce)[mfi];
      const Box& box = mfi.validbox();
      
      // Does a 0th order extrapolation (ie just takes the value from 
      //   nearest valid cell) to fill ghost cells
      FORT_VISCEXTRAP(sdcF.dataPtr(src_comp),ARLIM(sdcF.loVect()),ARLIM(sdcF.hiVect()),
		      box.loVect(),box.hiVect(),&num_comp);
   } 
    
    (*sdcForce).FillBoundary(src_comp,num_comp);
    //
    // Note: this is a special periodic fill in that we want to
    // preserve the extrapolated grow values when periodic --
    // usually we preserve only valid data.  The scheme relies on
    // the fact that there is good data in the "non-periodic" grow cells.
    // ("good" data produced via VISCEXTRAP above)
    //
    geom.FillPeriodicBoundary(*sdcForce,src_comp,num_comp,true);
}


//
// CEG:: this version of rhoh advection seems kinda wasteful
//   because it keeps recomputing terms at time n (which aren't 
//   changing).  However, the fluxes used in computing these 
//   NULN terms in the rhoh eqn are used for refluxing--and would 
//   have to get stored some place if i use a new fn 
//   compute_rhoh_NULN_terms() to avoid recomputing stuff at time n.
//
// void
// HeatTransfer::rhoh_advection_sdc (Real dt,
// 				  bool do_adv_reflux)
// {

//     MultiFab** fluxNULN_n;
//     MultiFab** fluxNULN_np1;
//     //
//     // compute non-unity Lewis number flux addition.stuff
//     // (using LinOp stuff which works on MultiFabs, so need to do this prior
//     // to the subsequent MFIter loop)
//     //
//     // Note that this requires reasonable species and Temperature values
//     //  in the state at np1, and pulls transport coeffs from the level. 
//     //  It should be that the coeffs in the level have been computed with the
//     //  temp and species in the state (to preserve isothermal flow over time step)
//     //
//     MultiFab Soln(grids,1,1);
//     const Real prev_time = state[State_Type].prevTime();
//     const Real cur_time  = state[State_Type].curTime();
//     MultiFab& S_new = get_new_data(State_Type);
//     MultiFab& S_old = get_old_data(State_Type);
//     //
//     // For each species, make a LinOp that can compute -(lambda/cp).A.Grad(Y_l)
//     // (the ViscBndry construction knows how to fill bc's correctly, so
//     //    re-construct linop/bndrydata for each species)
//     //
//     const Real a = 1.0;     // Passed around, but not used
//     Real rhsscale;          //  -ditto-
//     const int rho_flag = 2; // FIXME: Messy assumption
//     MultiFab *alpha=0;      //  -ditto-
//     MultiFab **fluxSC, **fluxi, **rhoh_visc;
//     diffusion->allocFluxBoxesLevel(fluxSC,0,1);
//     diffusion->allocFluxBoxesLevel(fluxi,0,nspecies);
//     diffusion->allocFluxBoxesLevel(fluxNULN_n,0,1);
//     diffusion->allocFluxBoxesLevel(fluxNULN_np1,0,1);
//     diffusion->allocFluxBoxesLevel(rhoh_visc,0,1);

//     const int nGrow    = 1; // Size to grow fil-patched fab for T below
//     const int dataComp = 0; // coeffs loaded into 0-comp for all species

    //Now using compute_NULN ()

    //
    // Initialize fluxNULN (NULN = non-unity Lewis number)
    //
//     for (int d = 0; d < BL_SPACEDIM; ++d)
//       fluxNULN_n[d]->setVal(0.0);
//     //
//     // Get the NULN flux contrib from n data
//     //
//     getDiffusivity(rhoh_visc, prev_time, RhoH, 0, 1);

//     const MultiFab* Rh = get_rho_half_time();

//     for (int comp = 0; comp < nspecies; ++comp)
//     {
//       const Real b     = 1.0 - be_cn_theta;
//       //      const Real b = -0.5;
//       const int  sigma = first_spec + comp;
//       //
//       // Start by getting lambda/cp.Grad(Y) (note: neg of usual diff flux)
//       //
//       ViscBndry      visc_bndry;
//       ABecLaplacian* visc_op;
      
//       visc_op = diffusion->getViscOp(sigma,a,b,prev_time,visc_bndry,
// 				     Rh,rho_flag,&rhsscale,dataComp,
// 				     rhoh_visc,alpha);
      
//       visc_op->maxOrder(diffusion->maxOrder());
//       MultiFab::Copy(Soln,S_old,sigma,0,1,0);
      
//       for (MFIter Smfi(Soln); Smfi.isValid(); ++Smfi)
// 	Soln[Smfi].divide(S_old[Smfi],Smfi.validbox(),Density,0,1);
      
//       visc_op->compFlux(D_DECL(*fluxSC[0],*fluxSC[1],*fluxSC[2]),Soln);
//       for (int d=0; d < BL_SPACEDIM; ++d)
// 	fluxSC[d]->mult(-b/geom.CellSize()[d]);
//       //
//       // Here, get fluxi = (lambda/cp - rho.D)Grad(Y)
//       //                 = lambda/cp.Grad(Y) + SpecDiffFlux
//       //
//       for (int d = 0; d < BL_SPACEDIM; ++d)
//       {
// 	for (MFIter SDF_mfi(*SpecDiffusionFluxn[d]);
// 	     SDF_mfi.isValid();
// 	     ++SDF_mfi)
// 	{
// 	  const Box& ebox    = SDF_mfi.validbox();
// 	  FArrayBox& SDF_fab = (*SpecDiffusionFluxn[d])[SDF_mfi];
// 	  (*fluxi[d])[SDF_mfi].copy(SDF_fab,ebox,comp,ebox,comp,1);
// 	  (*fluxi[d])[SDF_mfi].plus((*fluxSC[d])[SDF_mfi],ebox,0,comp,1);
// 	}
//       }
//       delete visc_op;
//     }
//     //
//     // Multiply fluxi by h_i, and add to running total.
//     //
//     for (FillPatchIterator Told_fpi(*this,S_old,nGrow,prev_time,State_Type,Temp,1);
// 	 Told_fpi.isValid();
// 	 ++Told_fpi)
//     {
//       const int i    = Told_fpi.index();
//       const Box& box = Told_fpi.validbox();
      
//       FArrayBox eTemp, h;
      
//       for (int d = 0; d < BL_SPACEDIM; ++d)
// 	{
// 	  const Box ebox = BoxLib::surroundingNodes(box,d);
// 	  eTemp.resize(ebox,1);
// 	  FPLoc bc_lo =
// 	    fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).lo(d));
// 	  FPLoc bc_hi = 
// 	    fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).hi(d));
	  
// 	  center_to_edge_fancy(Told_fpi(),eTemp,
// 			       BoxLib::grow(box,BoxLib::BASISV(d)),0,0,1,
// 			       geom.Domain(),bc_lo,bc_hi);
	  
// 	  h.resize(ebox,nspecies);
// 	  getChemSolve().getHGivenT(h,eTemp,ebox,0,0);
// 	  (*fluxi[d])[i].mult(h,ebox,0,0,nspecies);
          
// 	  for (int comp=0; comp<nspecies; ++comp)
// 	    (*fluxNULN_n[d])[i].plus((*fluxi[d])[i],ebox,comp,0,1);
// 	}
//     }
//     //
//     // Get the Le!=1 flux contrib from n+1 data.
//     //
//     for (int d = 0; d < BL_SPACEDIM; ++d)
//       fluxNULN_np1[d]->setVal(0.0);
    
//     getDiffusivity(rhoh_visc, cur_time, RhoH, 0, 1);
    
//     for (int comp = 0; comp < nspecies; ++comp)
//     {
//       const Real b     = be_cn_theta;
//       const int  sigma = first_spec + comp;
//       //
//       //  start by getting lambda/cp.Grad(Y) (note: neg of usual diff flux)
//       //
//       ViscBndry      visc_bndry;
//       ABecLaplacian* visc_op;
      
//       visc_op = diffusion->getViscOp(sigma,a,b,cur_time,visc_bndry,
// 				     Rh,rho_flag,&rhsscale,dataComp,
// 				     rhoh_visc,alpha);
      
//       visc_op->maxOrder(diffusion->maxOrder());
//       MultiFab::Copy(Soln,S_new,sigma,0,1,0);
      
//       for (MFIter Smfi(Soln); Smfi.isValid(); ++Smfi)
// 	Soln[Smfi].divide(S_new[Smfi],Smfi.validbox(),Density,0,1);

//       visc_op->compFlux(D_DECL(*fluxSC[0],*fluxSC[1],*fluxSC[2]),Soln);
//       for (int d=0; d < BL_SPACEDIM; ++d)
// 	fluxSC[d]->mult(-b/geom.CellSize()[d]);
//       //
//       // Here, get fluxi = (lambda/cp - rho.D)Grad(Y)
//       //                 = lambda/cp.Grad(Y) + SpecDiffFlux
//       //
//       for (int d = 0; d < BL_SPACEDIM; ++d)
//       {
// 	MFIter SDF_mfi(*SpecDiffusionFluxnp1[d]);
// 	for ( ; SDF_mfi.isValid(); ++SDF_mfi)
// 	{
// 	  FArrayBox& SDF_fab = (*SpecDiffusionFluxnp1[d])[SDF_mfi];
// 	  const Box& ebox    = SDF_mfi.validbox();
// 	  (*fluxi[d])[SDF_mfi].copy(SDF_fab,ebox,comp,ebox,comp,1);
// 	  (*fluxi[d])[SDF_mfi].plus((*fluxSC[d])[SDF_mfi],ebox,0,comp,1);
// 	}
//       }
//       delete visc_op;
//     }

//     Soln.clear();
//     diffusion->removeFluxBoxesLevel(fluxSC);
//     diffusion->removeFluxBoxesLevel(rhoh_visc);
//     //
//     // Multiply fluxi by h_i, and add to running total
//     //
//     for (FillPatchIterator Tnew_fpi(*this,S_new,nGrow,cur_time,State_Type,Temp,1);
// 	 Tnew_fpi.isValid();
// 	 ++Tnew_fpi)
//     {
//       const int i    = Tnew_fpi.index();
//       const Box& box = Tnew_fpi.validbox();
      
//       FArrayBox eTemp, h;
      
//       for (int d = 0; d < BL_SPACEDIM; ++d)
//       {
// 	const Box ebox = BoxLib::surroundingNodes(box,d);
// 	eTemp.resize(ebox,1);
// 	FPLoc bc_lo =
// 	  fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).lo(d));
// 	FPLoc bc_hi = 
// 	  fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).hi(d));
	
// 	center_to_edge_fancy(Tnew_fpi(),eTemp,BoxLib::grow(box,BoxLib::BASISV(d)),
// 			     0,0,1,geom.Domain(),bc_lo,bc_hi);
	
// 	h.resize(ebox,nspecies);
// 	getChemSolve().getHGivenT(h,eTemp,ebox,0,0);
// 	(*fluxi[d])[i].mult(h,ebox,0,0,nspecies);
        
// 	for (int comp = 0; comp < nspecies; ++comp)
// 	  (*fluxNULN_np1[d])[i].plus((*fluxi[d])[i],ebox,comp,0,1);
//       }
//     }  

//     diffusion->removeFluxBoxesLevel(fluxi);

//     Box edge_bx[BL_SPACEDIM];

//     FArrayBox edge[BL_SPACEDIM], area[BL_SPACEDIM], volume;

//     for (MFIter AofS_mfi(*aofs); AofS_mfi.isValid(); ++AofS_mfi)
//     {
//         const int i = AofS_mfi.index();

//         for (int d=0; d<BL_SPACEDIM; ++d)
//         {
//             edge_bx[d] = BoxLib::surroundingNodes(grids[i],d);
//             edge[d].resize(edge_bx[d],1);
//         }
//         for (int dir = 0; dir < BL_SPACEDIM; dir++)
//         {
//             geom.GetFaceArea(area[dir],grids,i,dir,GEOM_GROW);
//         }
//         geom.GetVolume(volume,grids,i,GEOM_GROW);

// 	// 
// 	// If here, edge states at n+1/2 have already been computed, get a copy
// 	// 
// 	for (int d=0; d<BL_SPACEDIM; ++d)
// 	  edge[d].copy((*EdgeState[d])[i],edge_bx[d],RhoH,edge_bx[d],0,1);
            
// 	int use_conserv_diff = 
// 	  (advectionType[RhoH] == Conservative) ? true : false;

// 	godunov->ComputeAofs(grids[i],
// 			     area[0],u_mac[0][i],edge[0],
// 			     area[1],u_mac[1][i],edge[1],
// #if BL_SPACEDIM==3
// 			     area[2],u_mac[2][i],edge[2],
// #endif
// 			     volume,(*aofs)[i],RhoH,
// 			     use_conserv_diff);
// 	//
// 	// Add divergence of fluxNULN to aofs[RhoH], and increment advective
// 	//  going into flux registers
// 	//
// 	if (do_adv_reflux)
// 	  for (int d=0; d<BL_SPACEDIM; ++d)
// 	  {
// 	      edge[d].plus((*fluxNULN_n[d])[i],edge_bx[d],0,0,1);
// 	      edge[d].plus((*fluxNULN_np1[d])[i],edge_bx[d],0,0,1);
// 	  }

// 	// NULN terms at time n
// 	{  // limit scope so staten can be reassigned below
// 	  (RhoH_NULN_terms[0])[i] = 0.0;
// 	  FArrayBox& staten = (RhoH_NULN_terms[0])[i];
// 	  const Box& box = AofS_mfi.validbox();
// 	  const FArrayBox& vol = volume;
// 	  const Real mult = 1.0; // no dt scaling of aofs, done in scl_adv_upd
// 	  const int nComp = 1;
// 	  //CEG:: am i screwing things up by not storing the NULN 
// 	  //   stuff in AofS???
// 	  FORT_INCRWEXTFLXDIV(box.loVect(), box.hiVect(),
// 			      (*fluxNULN_n[0])[i].dataPtr(),
// 			      ARLIM((*fluxNULN_n[0])[i].loVect()),
// 			      ARLIM((*fluxNULN_n[0])[i].hiVect()),
// 			      (*fluxNULN_n[1])[i].dataPtr(),
// 			      ARLIM((*fluxNULN_n[1])[i].loVect()),
// 			      ARLIM((*fluxNULN_n[1])[i].hiVect()),
// #if BL_SPACEDIM == 3
// 			      (*fluxNULN_n[2])[i].dataPtr(),
// 			      ARLIM((*fluxNULN_n[2])[i].loVect()),
// 			      ARLIM((*fluxNULN_n[2])[i].hiVect()),
// #endif
// 			      staten.dataPtr(),
// 			      ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
// 			      staten.dataPtr(),
// 			      ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
// 			      vol.dataPtr(),
// 			      ARLIM(vol.loVect()), ARLIM(vol.hiVect()),
// 			      &nComp, &mult);	    
// 	}
// 	// NULN terms at time n+1   
// 	(RhoH_NULN_terms[1])[i]= 0.0;
// 	FArrayBox& staten = (RhoH_NULN_terms[1])[i];
// 	const FArrayBox& vol = volume;
// 	const Box& box = AofS_mfi.validbox();
// 	const Real mult = 1.0; // no dt scaling of aofs, done in scl_adv_upd
// 	const int nComp = 1;

// 	FORT_INCRWEXTFLXDIV(box.loVect(), box.hiVect(),
// 			    (*fluxNULN_np1[0])[i].dataPtr(),
// 			    ARLIM((*fluxNULN_np1[0])[i].loVect()),
// 			    ARLIM((*fluxNULN_np1[0])[i].hiVect()),
// 			    (*fluxNULN_np1[1])[i].dataPtr(),
// 			    ARLIM((*fluxNULN_np1[1])[i].loVect()),
// 			    ARLIM((*fluxNULN_np1[1])[i].hiVect()),
// #if BL_SPACEDIM == 3
// 			    (*fluxNULN_np1[2])[i].dataPtr(),
// 			    ARLIM((*fluxNULN_np1[2])[i].loVect()),
// 			    ARLIM((*fluxNULN_np1[2])[i].hiVect()),
// #endif
// 			    staten.dataPtr(),
// 			    ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
// 			    staten.dataPtr(),
// 			    ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
// 			    vol.dataPtr(),
// 			    ARLIM(vol.loVect()), ARLIM(vol.hiVect()),
// 			    &nComp, &mult);	    

// // 	MultiFab::Add(*aofs,RhoH_NULN_terms[0],0,RhoH,1,0);
// // 	MultiFab::Add(*aofs,RhoH_NULN_terms[1],0,RhoH,1,0);
// // 	VisMF::Write(*aofs,"aofs_sdc");
// 	//
// 	// Do refluxing
// 	//
// 	if (do_adv_reflux)
// 	  pullFluxes(i,RhoH,1,edge[0],edge[1],edge[2],dt);
        
//     }

//     D_TERM(area[0].clear();, area[1].clear();, area[2].clear(););
//     D_TERM(edge[0].clear();, edge[1].clear();, edge[2].clear(););
    
//     diffusion->removeFluxBoxesLevel(fluxNULN_n);
//     diffusion->removeFluxBoxesLevel(fluxNULN_np1);
    //
    // pullFluxes() contains CrseInit() calls. Got to complete the process.
    //
//     if (do_reflux && level < parent->finestLevel())
//         getAdvFluxReg(level+1).CrseInitFinish();
// }


 void
HeatTransfer::rhoh_advection_sdc (Real dt,
				  bool do_adv_reflux)
{
    Box edge_bx[BL_SPACEDIM];
    FArrayBox edge[BL_SPACEDIM], area[BL_SPACEDIM], volume;

    for (MFIter AofS_mfi(*aofs); AofS_mfi.isValid(); ++AofS_mfi)
    {
        const int i = AofS_mfi.index();

        for (int dir = 0; dir < BL_SPACEDIM; dir++)
        {
            geom.GetFaceArea(area[dir],grids,i,dir,GEOM_GROW);    

	    edge_bx[dir] = BoxLib::surroundingNodes(grids[i],dir);
	    edge[dir].resize(edge_bx[dir],1);
        }
        geom.GetVolume(volume,grids,i,GEOM_GROW);
            
	for (int d=0; d<BL_SPACEDIM; ++d)
 	  edge[d].copy((*EdgeState[d])[i],edge_bx[d],RhoH,edge_bx[d],0,1);
   
	int use_conserv_diff = 
	  (advectionType[RhoH] == Conservative) ? true : false;

	godunov->ComputeAofs(grids[i],
			     area[0],u_mac[0][i],edge[0],
			     area[1],u_mac[1][i],edge[1],
#if BL_SPACEDIM==3
			     area[2],u_mac[2][i],edge[2],
#endif
			     volume,(*aofs)[i],RhoH,
			     use_conserv_diff);
	if (do_adv_reflux){
	  // store fluxes for refluxing done in compute_NULN_terms
	  for (int d=0; d<BL_SPACEDIM; ++d)
	    (*reflux_terms[d])[i].plus(edge[d],edge_bx[d],0,0,1);
	}

    }	

    D_TERM(area[0].clear();, area[1].clear();, area[2].clear(););
    D_TERM(edge[0].clear();, edge[1].clear();, edge[2].clear(););

}


// Rather than computing these terms at time n over again 
//   would make more sense to use this function
// This function computes
//     - div(h_m (rho*D_m - lambda/c_pmix) grad Y_m)
//
void
  HeatTransfer::compute_rhoh_NULN_terms (Real time,
					 Real dt, 
					 bool do_adv_reflux,
					 MultiFab& NULN_terms,
					 bool save_flux)
{
    //
    // Compute the advection flux divergences
    //
    MultiFab** fluxNULN;
    MultiFab Soln(grids,1,1);
    MultiFab& S = get_data(State_Type,time);

    // FIXME:: for using Radau, will have to start storing more
    //   flux arrays.   might be easier to just pass SpecFlux in
    //   as a parameter.
     const TimeLevel whichTime = which_time(State_Type,time);
     BL_ASSERT(whichTime == AmrOldTime || whichTime == AmrNewTime);
     MultiFab** SDF = (whichTime == AmrOldTime) ? 
       SpecDiffusionFluxn : SpecDiffusionFluxnp1;
    //
    // Compute non-unity Lewis number flux addition.stuff
    // (using LinOp stuff which works on MultiFabs, so need to do 
    // this prior to the subsequent MFIter loop)
    //
    // Note that this requires reasonable species and Temperature values
    //  in the state at np1, and pulls transport coeffs from the level. 
    //  It should be that the coeffs in the level have been computed 
    //  with the temp and species in the state (to preserve isothermal 
    //  flow over time step)
    //
    // For each species, make a LinOp that can compute 
    //  -(lambda/cp).A.Grad(Y_l)
    // (the ViscBndry construction knows how to fill bc's correctly, so
    //    re-construct linop/bndrydata for each species)
    //
    const Real a = 1.0;     // Passed around, but not used
    Real rhsscale;          //  -ditto-
    const int rho_flag = 2; // FIXME: Messy assumption
    MultiFab *alpha=0;      //  -ditto-
    MultiFab **fluxSC, **fluxi, **rhoh_visc;
    //                             MF,    nGhost, nVar
    diffusion->allocFluxBoxesLevel(fluxSC,0,      1);
    diffusion->allocFluxBoxesLevel(fluxi,0,nspecies);
    diffusion->allocFluxBoxesLevel(fluxNULN,0,1);
    diffusion->allocFluxBoxesLevel(rhoh_visc,0,1);

    const int nGrow    = 1; // Size to grow fil-patched fab for T below
    const int dataComp = 0; // coeffs loaded into 0-comp for all species

     VisMF::Write(*SDF[0],"specfluxx_sdc");
     VisMF::Write(*SDF[1],"specfluxy_sdc");

    //
    // Initialize fluxNULN (NULN = non-unity Lewis number)
    //
    for (int d = 0; d < BL_SPACEDIM; ++d)
      fluxNULN[d]->setVal(0.0);
 
   getDiffusivity(rhoh_visc, time, RhoH, 0, 1);
//         std::cout<<"time = "<<time<<std::endl;      
// 	VisMF::Write(*rhoh_visc[0],"NULNx_sdc");
// 	VisMF::Write(*rhoh_visc[1],"NULNy_sdc");
    
    //CEG:: FIXME when doing Radau, need to re-do get_rho_half_time()
    //  why use rho_half?  why not rho at time n or n+1???
    //  NavierStokes has MF* rho_ctime, rho_ptime
    const MultiFab* Rh = get_rho_half_time();
    
    for (int comp = 0; comp < nspecies; ++comp)
    {
      //FIXME debugging
      //const Real b     = 1.0 - be_cn_theta;
      const Real b     = 1.0;
      const int  sigma = first_spec + comp;
      //
      // Start by getting lambda/cp.Grad(Y) (note: neg of usual diff flux)
      //
      ViscBndry      visc_bndry;
      ABecLaplacian* visc_op;

      visc_op = diffusion->getViscOp(sigma,a,b,time,visc_bndry,
				     Rh,rho_flag,&rhsscale,dataComp,
				     rhoh_visc,alpha);
      
      visc_op->maxOrder(diffusion->maxOrder());
      MultiFab::Copy(Soln,S,sigma,0,1,0);
      
      for (MFIter Smfi(Soln); Smfi.isValid(); ++Smfi)
	Soln[Smfi].divide(S[Smfi],Smfi.validbox(),Density,0,1);
      
      visc_op->compFlux(D_DECL(*fluxSC[0],*fluxSC[1],*fluxSC[2]),Soln);
      for (int d=0; d < BL_SPACEDIM; ++d)
	fluxSC[d]->mult(-b/geom.CellSize()[d]);
	//	fluxSC[d]->mult(b/geom.CellSize()[d]);
      //
      // Here, get fluxi = (lambda/cp - rho.D)Grad(Y)
      //                 = lambda/cp.Grad(Y) + SpecDiffFlux
      //
      for (int d = 0; d < BL_SPACEDIM; ++d)
      {
	for (MFIter SDF_mfi(*SDF[d]); SDF_mfi.isValid(); ++SDF_mfi)
	{
	  const Box& ebox    = SDF_mfi.validbox();
	  FArrayBox& SDF_fab = (*SDF[d])[SDF_mfi];
 	  (*fluxi[d])[SDF_mfi].copy(SDF_fab,ebox,comp,ebox,comp,1);
	  // REMOVE ME debugging
	  // looks like theta already multiplied into SpecFlux
 	  (*fluxi[d])[SDF_mfi].mult(2.,ebox,comp,1);
 	  (*fluxi[d])[SDF_mfi].plus((*fluxSC[d])[SDF_mfi],ebox,0,comp,1);
//  	  (*fluxi[d])[SDF_mfi].copy((*fluxSC[d])[SDF_mfi],ebox,0,
// 				    ebox,comp,1);
//  	  (*fluxi[d])[SDF_mfi].minus(SDF_fab,ebox,comp,ebox,comp,1);
	}
      }
      delete visc_op;
    }

    Soln.clear();
    diffusion->removeFluxBoxesLevel(fluxSC);
    diffusion->removeFluxBoxesLevel(rhoh_visc);
    //
    // Multiply fluxi by h_i, and add to running total.
    //
    for (FillPatchIterator T_fpi(*this,S,nGrow,time,State_Type,Temp,1);
	 T_fpi.isValid();
	 ++T_fpi)
    {
      const int i    = T_fpi.index();
      const Box& box = T_fpi.validbox();
      
      FArrayBox eTemp, h;
      
      for (int d = 0; d < BL_SPACEDIM; ++d)
      {
	const Box ebox = BoxLib::surroundingNodes(box,d);
	eTemp.resize(ebox,1);
	FPLoc bc_lo =
	  fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).lo(d));
	FPLoc bc_hi = 
	  fpi_phys_loc(get_desc_lst()[State_Type].getBC(Temp).hi(d));
	
	center_to_edge_fancy(T_fpi(),eTemp,
			     BoxLib::grow(box,BoxLib::BASISV(d)),0,0,1,
			     geom.Domain(),bc_lo,bc_hi);
	
	h.resize(ebox,nspecies);
	getChemSolve().getHGivenT(h,eTemp,ebox,0,0);
	(*fluxi[d])[i].mult(h,ebox,0,0,nspecies);
        
	for (int comp=0; comp<nspecies; ++comp)
	  (*fluxNULN[d])[i].plus((*fluxi[d])[i],ebox,comp,0,1);
      }
    }

    diffusion->removeFluxBoxesLevel(fluxi);
//     VisMF::Write(*fluxNULN[0],"NULN1x_sdc");
//     VisMF::Write(*fluxNULN[1],"NULN1y_sdc");

    if (save_flux){
      for (int d=0; d<BL_SPACEDIM; ++d){
	MultiFab::Copy(*reflux_terms[d],*fluxNULN[d],0,0,1,0);
	// need 1/2 (NULN_n + NULN_n+1)
	(*reflux_terms[d]).mult(0.5);
      }
//       VisMF::Write(*reflux_terms[0],"NULNx_sdc");
//       VisMF::Write(*reflux_terms[1],"NULNy_sdc");
//       std::cout<<"save flux abort"<<std::endl;
//       if (level==1) abort();
    }


    FArrayBox volume, dummy;

    for (MFIter nuln_mfi(NULN_terms); nuln_mfi.isValid();
	 ++nuln_mfi)
    {
      const int i = nuln_mfi.index();

        geom.GetVolume(volume,grids,i,GEOM_GROW);
	
	// Compute "extra" terms in RhoH eqn coming from having NULN
	//   at time n.  (ie compute the divergence of the flux and
	//   scale by 1/Vol
	NULN_terms[i]= 0.0;
	FArrayBox& staten = NULN_terms[i];
	const Box& box = nuln_mfi.validbox();
	const FArrayBox& vol = volume;
	const Real mult = 1.0; // no dt scaling of aofs, done in scl_adv_upd
	const int nComp = 1;
	
	FORT_INCRWEXTFLXDIV(box.loVect(), box.hiVect(),
			    (*fluxNULN[0])[i].dataPtr(),
			    ARLIM((*fluxNULN[0])[i].loVect()),
			    ARLIM((*fluxNULN[0])[i].hiVect()),
			    (*fluxNULN[1])[i].dataPtr(),
			    ARLIM((*fluxNULN[1])[i].loVect()),
			    ARLIM((*fluxNULN[1])[i].hiVect()),
#if BL_SPACEDIM == 3
			    (*fluxNULN[2])[i].dataPtr(),
			    ARLIM((*fluxNULN[2])[i].loVect()),
			    ARLIM((*fluxNULN[2])[i].hiVect()),
#endif
			    staten.dataPtr(),
			    ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
			    staten.dataPtr(),
			    ARLIM(staten.loVect()), ARLIM(staten.hiVect()),
			    vol.dataPtr(),
			    ARLIM(vol.loVect()), ARLIM(vol.hiVect()),
			    &nComp, &mult);
	
	// Do refluxing
	//
	//CEG:: Why are we doing this? For coarse-fine boundary 
	//  agreement on flux
	if (do_adv_reflux){
// 	  VisMF::Write(*reflux_terms[0],"NULNnx_sdc");
// 	  VisMF::Write(*reflux_terms[1],"NULNny_sdc");

// 	  std::ofstream edge_fab3("edgextot_sdc");
// 	  edge[0].writeOn(edge_fab3);
// 	  edge_fab3.close();
// 	  std::ofstream edge_fab4("edgeytot_sdc");
// 	  edge[1].writeOn(edge_fab4);
// 	  edge_fab4.close();

	  for (int d=0; d<BL_SPACEDIM; ++d){
	    Box edge_bx = BoxLib::surroundingNodes(grids[i],d);

	    (*fluxNULN[d])[i].mult(0.5);
       	    (*reflux_terms[d])[i].plus((*fluxNULN[d])[i]);
	  }
// 	  VisMF::Write(*fluxNULN[0],"NULNx_sdc");
// 	  VisMF::Write(*fluxNULN[1],"NULNy_sdc");

	  pullFluxes(i,RhoH,1,(*reflux_terms[0])[i],(*reflux_terms[1])[i],
#if (BL_SPACEDIM ==3)
		     (*reflux_terms[2])[i],
#else
		     dummy,
#endif
		     dt);
	}
    }

    diffusion->removeFluxBoxesLevel(fluxNULN);
    //
    // pullFluxes() contains CrseInit() calls. Got to complete the process.
    //
    if (do_reflux && level < parent->finestLevel())
      getAdvFluxReg(level+1).CrseInitFinish();
}

void
HeatTransfer::rhoh_diffusion_update_sdc (Real dt,
					 int  corrector)
{
    const Real strt_time = ParallelDescriptor::second();
    //
    // Build single component edge-centered array of MultiFabs for fluxes
    //
    MultiFab** fluxSCn;
    MultiFab** fluxSCnp1;
    const int nGrow = 0;
    const int nComp = 1;
    diffusion->allocFluxBoxesLevel(fluxSCn  ,nGrow,nComp);
    diffusion->allocFluxBoxesLevel(fluxSCnp1,nGrow,nComp);
    //
    // Set diffusion solve mode.
    //
    Diffusion::SolveMode solve_mode = Diffusion::ONEPASS;
    //
    // Do implicit c-n solve for each scalar.
    //
    const MultiFab* Rh = get_rho_half_time();

    //FIXME debugging
    //Real rhoh_theta = 1.0;
    Real rhoh_theta = 0.5;
    int rho_flag = 0;
    MultiFab *delta_rhs, *alpha, **betan, **betanp1;
    
    // diffuse_scalar_setup(dt,sigma,&rho_flag,delta_rhs,alpha,betan,betanp1);
    
    const Real prev_time = state[State_Type].prevTime();

    // this function only executes one line of code.
    // to set rho_flag
    // is this really neccessary?
    NavierStokes::diffuse_scalar_setup(dt, RhoH, &rho_flag, 
                                       delta_rhs, alpha, betan, betanp1);
    alpha     = 0;
    delta_rhs = 0;
    betan     = 0;
    betanp1   = 0;
   
    // diffusion type = laplacian(S/rho)
    rho_flag = 2;
    
    //        diffuse_rhoh_setup(prev_time,dt,delta_rhs); 
    BL_ASSERT(delta_rhs==0);
    delta_rhs = new MultiFab (grids,1,0);
    delta_rhs->setVal(0);

    // Add NULN terms (put in sdcForce in advance_sdc()) and any 
    //  sdc terms
    //    VisMF::Write(*sdcForce,"sdcf");
    MultiFab::Copy(*delta_rhs,*sdcForce,RhoH,0,1,0);      

    diffusion->allocFluxBoxesLevel(betan);
    diffusion->allocFluxBoxesLevel(betanp1);
    getDiffusivity(betan, prev_time, RhoH, 0, 1);
    getDiffusivity(betanp1, prev_time+dt, RhoH, 0, 1);

    const int dataComp = 0; // Component of dR, alpha, betas to use.

    //FIXME
//     VisMF::Write(*betan[0],"bxn_sdc");    
//     VisMF::Write(*betan[1],"byn_sdc");    
//     VisMF::Write(*betanp1[0],"bxn1_sdc");    
//     VisMF::Write(*betanp1[1],"byn1_sdc"); 
//     VisMF::Write(*fluxSCn[0],"fscxn_sdc");    
//     VisMF::Write(*fluxSCn[1],"fscyn_sdc");    
//     VisMF::Write(*fluxSCnp1[0],"fscxn1_sdc");    
//     VisMF::Write(*fluxSCnp1[1],"fscyn1_sdc");    
    //VisMF::Write(*alpha,"a_sdc");    
//     	      VisMF::Write(*Rh,"rh_sdc");       
// 	      VisMF::Write(*delta_rhs,"delta_sdc");       
// 	      std::cout<<" dt = "<<dt<<std::endl;
// 	      std::cout<<" theta = "<<be_cn_theta<<std::endl;
// 	      std::cout<<" rho_flag = "<<rho_flag<<std::endl;
// 	      std::cout<<" dataComp = "<<dataComp<<std::endl;
// 	      std::cout<<" solve mode = "<<solve_mode<<std::endl;


    // Note: dt taken care of in diffuse_scalar.
    //

    diffusion->diffuse_scalar(dt,RhoH,rhoh_theta,Rh,rho_flag,fluxSCn,
			      fluxSCnp1,dataComp,delta_rhs,alpha,betan,
			      betanp1,solve_mode);

    //
    // If corrector, increment the viscous flux registers (assume
    //  corrector called only ONCE!).
    //
    if (do_reflux && corrector)
      {
	{
	  FArrayBox fluxtot;
	  for (int d = 0; d < BL_SPACEDIM; d++)
	    {
	      for (MFIter fmfi(*fluxSCn[d]); fmfi.isValid(); ++fmfi)
		{
		  const Box& ebox = (*fluxSCn[d])[fmfi].box();
		  fluxtot.resize(ebox,nComp);
		  fluxtot.copy((*fluxSCn[d])[fmfi],ebox,
			       0,ebox,0,nComp);
		  fluxtot.plus((*fluxSCnp1[d])[fmfi],ebox,
			       0,0,nComp);
		  if (level < parent->finestLevel())
		    getLevel(level+1).getViscFluxReg().
		      CrseInit(fluxtot,ebox,d,0,RhoH,nComp,-dt);
			
		  if (level > 0)
		    getViscFluxReg().FineAdd(fluxtot,d,
					     fmfi.index(),
					     0,RhoH,nComp,dt);
		}
	    }
	}
	if (level < parent->finestLevel())
	  getLevel(level+1).getViscFluxReg().CrseInitFinish();
      }
    //
    // Clean up memory, etc
    //
    diffuse_cleanup(delta_rhs, betan, betanp1, alpha);
      
    diffusion->removeFluxBoxesLevel(fluxSCn);
    diffusion->removeFluxBoxesLevel(fluxSCnp1);

    const int IOProc   = ParallelDescriptor::IOProcessorNumber();
    Real      run_time = ParallelDescriptor::second() - strt_time;
    
    ParallelDescriptor::ReduceRealMax(run_time,IOProc);
    
    if (verbose && ParallelDescriptor::IOProcessor())
        std::cout << "HeatTransfer::scalar_diffusion_update(): time: " << run_time << std::endl;
}
