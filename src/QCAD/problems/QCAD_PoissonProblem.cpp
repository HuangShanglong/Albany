//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "QCAD_PoissonProblem.hpp"
#include "QCAD_MaterialDatabase.hpp"
#include "Albany_Utils.hpp"

QCAD::PoissonProblem::
PoissonProblem( const Teuchos::RCP<Teuchos::ParameterList>& params_,
		const Teuchos::RCP<ParamLib>& paramLib_,
		const int numDim_,
		const Teuchos::RCP<const Epetra_Comm>& comm_) :
  Albany::AbstractProblem(params_, paramLib_, 1),
  comm(comm_),
  haveSource(false),
  numDim(numDim_)
{
  if (numDim==1) periodic = params->get("Periodic BC", false);
  else           periodic = false;
  if (periodic) *out <<" Periodic Boundary Conditions being used." <<std::endl;

  haveSource =  params->isSublist("Poisson Source");

  TEUCHOS_TEST_FOR_EXCEPTION(params->isSublist("Source Functions"), Teuchos::Exceptions::InvalidParameter,
		     "\nError! Poisson problem does not parse Source Functions sublist\n" 
                     << "\tjust Poisson Source sublist " << std::endl);

  //get length scale for problem (length unit for in/out mesh)
  length_unit_in_m = 1e-6; //default to um
  if(params->isType<double>("Length Unit In Meters"))
    length_unit_in_m = params->get<double>("Length Unit In Meters");

  //get energy (voltage) unit for problem
  energy_unit_in_eV = 1.0; //default to eV
  if(params->isType<double>("Energy Unit In Electron Volts"))
    energy_unit_in_eV = params->get<double>("Energy Unit In Electron Volts");

  temperature = 300; //default to 300K
  if(params->isType<double>("Temperature"))
    temperature = params->get<double>("Temperature");

  // Create Material Database
  std::string mtrlDbFilename = "materials.xml";
  if(params->isType<std::string>("MaterialDB Filename"))
    mtrlDbFilename = params->get<std::string>("MaterialDB Filename");
  materialDB = Teuchos::rcp(new QCAD::MaterialDatabase(mtrlDbFilename, comm));

  //Pull number of eigenvectors from poisson params list
  nEigenvectors = 0;
  Teuchos::ParameterList& psList = params->sublist("Poisson Source");
  if(psList.isType<int>("Eigenvectors to Import"))
    nEigenvectors = psList.get<int>("Eigenvectors to Import");


  /* Now just Poisson source params
  //Schrodinger coupling
  nEigenvectors = 0;
  bUseSchrodingerSource = false;
  bUsePredictorCorrector = false;
  bIncludeVxc = false; 
  
  if(params->isSublist("Schrodinger Coupling")) {
    Teuchos::ParameterList& cList = params->sublist("Schrodinger Coupling");
    if(cList.isType<bool>("Schrodinger source in quantum blocks"))
      bUseSchrodingerSource = cList.get<bool>("Schrodinger source in quantum blocks");
    *out << "bSchod in quantum = " << bUseSchrodingerSource << std::endl;
    
    if(bUseSchrodingerSource && cList.isType<int>("Eigenvectors from States"))
      nEigenvectors = cList.get<int>("Eigenvectors from States");
    
    if(bUseSchrodingerSource && cList.isType<bool>("Use predictor-corrector method"))
      bUsePredictorCorrector = cList.get<bool>("Use predictor-corrector method");

    if(bUseSchrodingerSource && cList.isType<bool>("Include exchange-correlation potential"))
      bIncludeVxc = cList.get<bool>("Include exchange-correlation potential");
  }*/

  *out << "Length unit = " << length_unit_in_m << " meters" << std::endl;
  *out << "Energy unit = " << energy_unit_in_eV << " electron volts" << std::endl;
}

QCAD::PoissonProblem::
~PoissonProblem()
{
}

void
QCAD::PoissonProblem::
buildProblem(
  Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >  meshSpecs,
  Albany::StateManager& stateMgr)
{
  /* Construct All Phalanx Evaluators */
  TEUCHOS_TEST_FOR_EXCEPTION(meshSpecs.size()!=1,std::logic_error,"Problem supports one Material Block");
  fm.resize(1);
  fm[0]  = Teuchos::rcp(new PHX::FieldManager<PHAL::AlbanyTraits>);
  buildEvaluators(*fm[0], *meshSpecs[0], stateMgr, Albany::BUILD_RESID_FM, 
		  Teuchos::null);
  constructDirichletEvaluators(*meshSpecs[0]);

  if(meshSpecs[0]->ssNames.size() > 0) // Build a sideset evaluator if sidesets are present
    constructNeumannEvaluators(meshSpecs[0]);

}

Teuchos::Array< Teuchos::RCP<const PHX::FieldTag> >
QCAD::PoissonProblem::
buildEvaluators(
  PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
  const Albany::MeshSpecsStruct& meshSpecs,
  Albany::StateManager& stateMgr,
  Albany::FieldManagerChoice fmchoice,
  const Teuchos::RCP<Teuchos::ParameterList>& responseList)
{
  // Call constructeEvaluators<EvalT>(*rfm[0], *meshSpecs[0], stateMgr);
  // for each EvalT in PHAL::AlbanyTraits::BEvalTypes
  Albany::ConstructEvaluatorsOp<PoissonProblem> op(
    *this, fm0, meshSpecs, stateMgr, fmchoice, responseList);
  boost::mpl::for_each<PHAL::AlbanyTraits::BEvalTypes>(op);
  return *op.tags;
}

void
QCAD::PoissonProblem::constructDirichletEvaluators(
     const Albany::MeshSpecsStruct& meshSpecs)
{
   using Teuchos::RCP;
   using Teuchos::rcp;
   using Teuchos::ParameterList;
   using PHX::DataLayout;
   using PHX::MDALayout;
   using std::vector;
   using std::map;
   using std::string;

   using PHAL::DirichletFactoryTraits;
   using PHAL::AlbanyTraits;

   // Construct Dirichlet evaluators for all nodesets and names
   vector<string> dirichletNames(neq);
   dirichletNames[0] = "Phi";   
//   Albany::BCUtils<Albany::DirichletTraits> dirUtils;

   const std::vector<std::string>& nodeSetIDs = meshSpecs.nsNames;

   Teuchos::ParameterList DBCparams = params->sublist("Dirichlet BCs");
   DBCparams.validateParameters(*(Albany::DirichletTraits::getValidBCParameters(nodeSetIDs,dirichletNames)),0);

   map<string, RCP<ParameterList> > evaluators_to_build;
   RCP<DataLayout> dummy = rcp(new MDALayout<Dummy>(0));
   vector<string> dbcs;

   // Check for all possible standard BCs (every dof on every nodeset) to see which is set
   for (std::size_t i=0; i<nodeSetIDs.size(); i++) {
     for (std::size_t j=0; j<dirichletNames.size(); j++) {

       std::stringstream sstrm; sstrm << "DBC on NS " << nodeSetIDs[i] << " for DOF " << dirichletNames[j];
       std::string ss = sstrm.str();

       if (DBCparams.isParameter(ss)) {
         RCP<ParameterList> p = rcp(new ParameterList);
         int type = DirichletFactoryTraits<AlbanyTraits>::id_qcad_poisson_dirichlet;
         p->set<int>("Type", type);

         p->set< RCP<DataLayout> >("Data Layout", dummy);
         p->set< string >  ("Dirichlet Name", ss);
         p->set< RealType >("Dirichlet Value", DBCparams.get<double>(ss));
         p->set< string >  ("Node Set ID", nodeSetIDs[i]);
         p->set< int >     ("Number of Equations", dirichletNames.size());
         p->set< int >     ("Equation Offset", j);

         p->set<RCP<ParamLib> >("Parameter Library", paramLib);

         //! Additional parameters needed for Poisson Dirichlet BCs
         Teuchos::ParameterList& paramList = params->sublist("Poisson Source");
         p->set<Teuchos::ParameterList*>("Poisson Source Parameter List", &paramList);
         //p->set<string>("Temperature Name", "Temperature");  //to add if use shared param for DBC
         p->set<double>("Temperature", temperature);
         p->set< RCP<QCAD::MaterialDatabase> >("MaterialDB", materialDB);
	 p->set<double>("Energy unit in eV", energy_unit_in_eV);

         std::stringstream ess; ess << "Evaluator for " << ss;
         evaluators_to_build[ess.str()] = p;

         dbcs.push_back(ss);
       }
     }
   }

   //From here down, identical to Albany::AbstractProblem version of this function
   string allDBC="Evaluator for all Dirichlet BCs";
   {
      RCP<ParameterList> p = rcp(new ParameterList);
      int type = DirichletFactoryTraits<AlbanyTraits>::id_dirichlet_aggregator;
      p->set<int>("Type", type);

      p->set<vector<string>* >("DBC Names", &dbcs);
      p->set< RCP<DataLayout> >("Data Layout", dummy);
      p->set<string>("DBC Aggregator Name", allDBC);
      evaluators_to_build[allDBC] = p;
   }

   // Build Field Evaluators for each evaluation type
   PHX::EvaluatorFactory<AlbanyTraits,DirichletFactoryTraits<AlbanyTraits> > factory;
   RCP< vector< RCP<PHX::Evaluator_TemplateManager<AlbanyTraits> > > > evaluators;
   evaluators = factory.buildEvaluators(evaluators_to_build);

   // Create a DirichletFieldManager
   dfm = Teuchos::rcp(new PHX::FieldManager<AlbanyTraits>);

   // Register all Evaluators
   PHX::registerEvaluators(evaluators, *dfm);

   PHX::Tag<AlbanyTraits::Residual::ScalarT> res_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::Residual>(res_tag0);

   PHX::Tag<AlbanyTraits::Jacobian::ScalarT> jac_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::Jacobian>(jac_tag0);

   PHX::Tag<AlbanyTraits::Tangent::ScalarT> tan_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::Tangent>(tan_tag0);

#ifdef ALBANY_SG_MP
   PHX::Tag<AlbanyTraits::SGResidual::ScalarT> sgres_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::SGResidual>(sgres_tag0);

   PHX::Tag<AlbanyTraits::SGJacobian::ScalarT> sgjac_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::SGJacobian>(sgjac_tag0);

   PHX::Tag<AlbanyTraits::SGTangent::ScalarT> sgtan_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::SGTangent>(sgtan_tag0);

   PHX::Tag<AlbanyTraits::MPResidual::ScalarT> mpres_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::MPResidual>(mpres_tag0);

   PHX::Tag<AlbanyTraits::MPJacobian::ScalarT> mpjac_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::MPJacobian>(mpjac_tag0);

   PHX::Tag<AlbanyTraits::MPTangent::ScalarT> mptan_tag0(allDBC, dummy);
   dfm->requireField<AlbanyTraits::MPTangent>(mptan_tag0);
#endif //ALBANY_SG_MP
}

// Neumann BCs
void
QCAD::PoissonProblem::constructNeumannEvaluators(const Teuchos::RCP<Albany::MeshSpecsStruct>& meshSpecs)
{
   using std::string;
   // Note: we only enter this function if sidesets are defined in the mesh file
   // i.e. meshSpecs.ssNames.size() > 0

   Albany::BCUtils<Albany::NeumannTraits> bcUtils;

   // Check to make sure that Neumann BCs are given in the input file

   if(!bcUtils.haveBCSpecified(this->params))

      return;

   // Construct BC evaluators for all side sets and names
   // Note that the string index sets up the equation offset, so ordering is important
   std::vector<string> bcNames(neq);
   Teuchos::ArrayRCP<string> dof_names(neq);
   Teuchos::Array<Teuchos::Array<int> > offsets;
   offsets.resize(neq);

   bcNames[0] = "Phi";
   dof_names[0] = "Potential";
   offsets[0].resize(1);
   offsets[0][0] = 0;


   // Construct BC evaluators for all possible names of conditions
   // Should only specify flux vector components (dudx, dudy, dudz), or dudn, not both
   std::vector<string> condNames(4);
     //dudx, dudy, dudz, dudn, scaled jump (internal surface), or robin (like DBC plus scaled jump)

   // Note that sidesets are only supported for two and 3D currently
   if(numDim == 2)
    condNames[0] = "(dudx, dudy)";
   else if(numDim == 3)
    condNames[0] = "(dudx, dudy, dudz)";
   else
    TEUCHOS_TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter,
       std::endl << "Error: Sidesets only supported in 2 and 3D." << std::endl);

   condNames[1] = "dudn";

   condNames[2] = "scaled jump";

   condNames[3] = "robin";

   //nfm[0] = bcUtils.constructBCEvaluators(meshSpecs, bcNames, dof_names, false, 0,
   //				  condNames, offsets, dl, this->params, this->paramLib, materialDB);
   bool isVectorField = false;
   int offsetToFirstDOF = 0;

   // From here down, this code was copied from constructBCEvaluators call commented out
   //   above and modified to create QCAD::PoissonNeumann evaluators.
   using Teuchos::RCP;
   using Teuchos::rcp;
   using Teuchos::ParameterList;
   using PHX::DataLayout;
   using PHX::MDALayout;
   using std::vector;

   using PHAL::NeumannFactoryTraits;
   using PHAL::AlbanyTraits;

   // Drop into the "Neumann BCs" sublist
   Teuchos::ParameterList BCparams = this->params->sublist(Albany::NeumannTraits::bcParamsPl);
   BCparams.validateParameters(*(Albany::NeumannTraits::getValidBCParameters(meshSpecs->ssNames, bcNames, condNames)),0);
   
   
   std::map<string, RCP<ParameterList> > evaluators_to_build;
   vector<string> bcs;

   // Check for all possible standard BCs (every dof on every sideset) to see which is set
   for (std::size_t i=0; i<meshSpecs->ssNames.size(); i++) {
     for (std::size_t j=0; j<bcNames.size(); j++) {
       for (std::size_t k=0; k<condNames.size(); k++) {
	 
        // construct input.xml string like:
        // "NBC on SS sidelist_12 for DOF T set dudn"
        //  or
        // "NBC on SS sidelist_12 for DOF T set (dudx, dudy)"
        // or
        // "NBC on SS surface_1 for DOF all set P"

         std::string ss = Albany::NeumannTraits::constructBCName(meshSpecs->ssNames[i], bcNames[j], condNames[k]);

         // Have a match of the line in input.xml
	 
         if (BCparams.isParameter(ss)) {
	   
//           std::cout << "Constructing NBC: " << ss << std::endl;
	   
           TEUCHOS_TEST_FOR_EXCEPTION(BCparams.isType<string>(ss), std::logic_error,
				      "NBC array information in XML file must be of type Array(double)\n");
	   
           // These are read in the Albany::Neumann constructor (PHAL_Neumann_Def.hpp)
	   
           RCP<ParameterList> p = rcp(new ParameterList);
	   
	   int type = NeumannFactoryTraits<AlbanyTraits>::id_qcad_poisson_neumann;
	   p->set<int>                            ("Type", type);
	   
           p->set<RCP<ParamLib> >                 ("Parameter Library", this->paramLib);

	   //! Additional parameters needed for Poisson Dirichlet BCs	 
	   Teuchos::ParameterList& paramList = params->sublist("Poisson Source");
	   p->set<Teuchos::ParameterList*>("Poisson Source Parameter List", &paramList);
	   p->set<double>("Temperature", temperature);
	   p->set< RCP<QCAD::MaterialDatabase> >("MaterialDB", materialDB);
	   p->set<double>("Energy unit in eV", energy_unit_in_eV);

	   p->set<string>                         ("Side Set ID", meshSpecs->ssNames[i]);
	   p->set<Teuchos::Array< int > >         ("Equation Offset", offsets[j]);
	   p->set< RCP<Albany::Layouts> >         ("Layouts Struct", dl);
           p->set< RCP<Albany::MeshSpecsStruct> >         ("Mesh Specs Struct", meshSpecs);
	   
           p->set<string>                         ("Coordinate Vector Name", "Coord Vec");

           if(condNames[k] == "robin") {
             p->set<string>  ("DOF Name", dof_names[j]);
	     p->set<bool> ("Vector Field", isVectorField);
	     if (isVectorField) {p->set< RCP<DataLayout> >("DOF Data Layout", dl->node_vector);}
	     else               p->set< RCP<DataLayout> >("DOF Data Layout", dl->node_scalar);
           }
           else if(condNames[k] == "basal") {
             std::string betaName = BCparams.get("BetaXY", "Constant");
             double L = BCparams.get("L", 1.0);
             p->set<string> ("BetaXY", betaName); 
             p->set<double> ("L", L);   
             p->set<string>  ("DOF Name", dof_names[0]);
	     p->set<bool> ("Vector Field", isVectorField);
	     if (isVectorField) {p->set< RCP<DataLayout> >("DOF Data Layout", dl->node_vector);}
	     else               p->set< RCP<DataLayout> >("DOF Data Layout", dl->node_scalar);
           }

           // Pass the input file line
           p->set< string >                       ("Neumann Input String", ss);
           p->set< Teuchos::Array<double> >       ("Neumann Input Value", BCparams.get<Teuchos::Array<double> >(ss));
           p->set< string >                       ("Neumann Input Conditions", condNames[k]);

           // If we are doing a Neumann internal boundary with a "scaled jump" (includes "robin" too)
           // The material DB database needs to be passed to the BC object

           if(condNames[k] == "scaled jump" || condNames[k] == "robin"){ 

              TEUCHOS_TEST_FOR_EXCEPTION(materialDB == Teuchos::null,
                Teuchos::Exceptions::InvalidParameter, 
                "This BC needs a material database specified");

              p->set< RCP<QCAD::MaterialDatabase> >("MaterialDB", materialDB);


           }


    // Inputs: X, Y at nodes, Cubature, and Basis
    //p->set<string>("Node Variable Name", "Neumann");

           std::stringstream ess; ess << "Evaluator for " << ss;
           evaluators_to_build[ess.str()] = p;

  
           bcs.push_back(ss);
         }
       }
     }
   }


   // Build evaluator for Gather Coordinate Vector
   string NeuGCV="Evaluator for Gather Coordinate Vector";
   {
      RCP<ParameterList> p = rcp(new ParameterList);
      p->set<int>("Type", Albany::NeumannTraits::typeGCV);

      // Input: Periodic BC flag
      p->set<bool>("Periodic BC", false);
 
      // Output:: Coordindate Vector at vertices
      p->set< RCP<DataLayout> >  ("Coordinate Data Layout",  dl->vertices_vector);
      p->set< string >("Coordinate Vector Name", "Coord Vec");
 
      evaluators_to_build[NeuGCV] = p;
   }

   // Build evaluator for Gather Solution
   string NeuGS="Evaluator for Gather Solution";
   {
     RCP<ParameterList> p = rcp(new ParameterList());
     p->set<int>("Type", Albany::NeumannTraits::typeGS);
 
     // for new way
     p->set< RCP<Albany::Layouts> >("Layouts Struct", dl);

     p->set< Teuchos::ArrayRCP<std::string> >("Solution Names", dof_names);

     p->set<bool>("Vector Field", isVectorField);
     if (isVectorField) p->set< RCP<DataLayout> >("Data Layout", dl->node_vector);
     else               p->set< RCP<DataLayout> >("Data Layout", dl->node_scalar);

     p->set<int>("Offset of First DOF", offsetToFirstDOF);
     p->set<bool>("Disable Transient", true);

     evaluators_to_build[NeuGS] = p;
   }


   // Build evaluator that causes the evaluation of all the NBCs
   string allBC="Evaluator for all Neumann BCs";
   {
      RCP<ParameterList> p = rcp(new ParameterList);
      p->set<int>("Type", Albany::NeumannTraits::typeNa);

      p->set<vector<string>* >("NBC Names", &bcs);
      p->set< RCP<DataLayout> >("Data Layout", dl->dummy);
      p->set<string>("NBC Aggregator Name", allBC);
      evaluators_to_build[allBC] = p;
   }

   // Inlined call to:
   // nfm[0] = bcUtils.buildFieldManager(evaluators_to_build, allBC, dl->dummy);
   // since function is private -- consider making this public?

   // Build Field Evaluators for each evaluation type
   PHX::EvaluatorFactory<AlbanyTraits,PHAL::NeumannFactoryTraits<AlbanyTraits> > factory;
   RCP< vector< RCP<PHX::Evaluator_TemplateManager<AlbanyTraits> > > > evaluators;
   evaluators = factory.buildEvaluators(evaluators_to_build);

   // Create a FieldManager
   Teuchos::RCP<PHX::FieldManager<AlbanyTraits> > fm
     = Teuchos::rcp(new PHX::FieldManager<AlbanyTraits>);

   // Register all Evaluators
   PHX::registerEvaluators(evaluators, *fm);

   PHX::Tag<AlbanyTraits::Residual::ScalarT> res_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::Residual>(res_tag0);

   PHX::Tag<AlbanyTraits::Jacobian::ScalarT> jac_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::Jacobian>(jac_tag0);

   PHX::Tag<AlbanyTraits::Tangent::ScalarT> tan_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::Tangent>(tan_tag0);

#ifdef ALBANY_SG_MP
   PHX::Tag<AlbanyTraits::SGResidual::ScalarT> sgres_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::SGResidual>(sgres_tag0);

   PHX::Tag<AlbanyTraits::SGJacobian::ScalarT> sgjac_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::SGJacobian>(sgjac_tag0);

   PHX::Tag<AlbanyTraits::SGTangent::ScalarT> sgtan_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::SGTangent>(sgtan_tag0);

   PHX::Tag<AlbanyTraits::MPResidual::ScalarT> mpres_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::MPResidual>(mpres_tag0);

   PHX::Tag<AlbanyTraits::MPJacobian::ScalarT> mpjac_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::MPJacobian>(mpjac_tag0);

   PHX::Tag<AlbanyTraits::MPTangent::ScalarT> mptan_tag0(allBC, dl->dummy);
   fm->requireField<AlbanyTraits::MPTangent>(mptan_tag0);
#endif //ALBANY_SG_MP

   nfm = fm;
}

Teuchos::RCP<const Teuchos::ParameterList>
QCAD::PoissonProblem::getValidProblemParameters() const
{
  Teuchos::RCP<Teuchos::ParameterList> validPL =
    this->getGenericProblemParams("ValidPoissonProblemParams");

  if (numDim==1)
    validPL->set<bool>("Periodic BC", false, "Flag to indicate periodic BC for 1D problems");
  validPL->sublist("Permittivity", false, "");
  validPL->sublist("Poisson Source", false, "");
  validPL->set<double>("Length Unit In Meters",1e-6,"Length unit in meters");
  validPL->set<double>("Energy Unit In Electron Volts",1.0,"Energy (voltage) unit in electron volts for output values only (e.g. DBCs are still in volts)");
  validPL->set<double>("Temperature",300,"Temperature in Kelvin");
  validPL->set<std::string>("MaterialDB Filename","materials.xml","Filename of material database xml file");

  //validPL->sublist("Schrodinger Coupling", false, "");
  //validPL->sublist("Schrodinger Coupling").set<bool>("Schrodinger source in quantum blocks",false,"Use eigenvector data to compute charge distribution within quantum blocks");
  //validPL->sublist("Schrodinger Coupling").set<int>("Eigenvectors from States",0,"Number of eigenvectors to use for quantum region source");
  
  //For poisson schrodinger interations
  validPL->sublist("Dummy Dirichlet BCs", false, "");
  validPL->sublist("Dummy Parameters", false, "");

  return validPL;
}

