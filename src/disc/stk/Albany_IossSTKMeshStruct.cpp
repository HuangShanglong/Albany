//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//
#ifdef ALBANY_SEACAS

#include <iostream>

#include "Albany_IossSTKMeshStruct.hpp"
#include "Teuchos_VerboseObject.hpp"

#include <Shards_BasicTopologies.hpp>

#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/FieldBase.hpp>
#include <stk_mesh/base/Selector.hpp>
#include <stk_io/IossBridge.hpp>
#include <Ioss_SubSystem.h>

//#include <stk_mesh/fem/FEMHelpers.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "Albany_Utils.hpp"

namespace {

void get_element_block_sizes(stk::io::StkMeshIoBroker &mesh_data,
                             std::vector<int>& el_blocks)
{
  Ioss::Region &io = *mesh_data.get_input_io_region();
  const Ioss::ElementBlockContainer& elem_blocks = io.get_element_blocks();
  for(Ioss::ElementBlockContainer::const_iterator it = elem_blocks.begin(); it != elem_blocks.end(); ++it) {
    Ioss::ElementBlock *entity = *it;
    if (stk::io::include_entity(entity)) {
      el_blocks.push_back(entity->get_property("entity_count").get_int());
    }
  }
}

}

Albany::IossSTKMeshStruct::IossSTKMeshStruct(
                                             const Teuchos::RCP<Teuchos::ParameterList>& params, 
                                             const Teuchos::RCP<Teuchos::ParameterList>& adaptParams_, 
                                             const Teuchos::RCP<const Epetra_Comm>& comm) :
  GenericSTKMeshStruct(params, adaptParams_),
  out(Teuchos::VerboseObjectBase::getDefaultOStream()),
  useSerialMesh(false),
  periodic(params->get("Periodic BC", false)),
  m_hasRestartSolution(false),
  m_restartDataTime(-1.0),
  m_solutionFieldHistoryDepth(0)
{
  params->validateParameters(*getValidDiscretizationParameters(),0);

  usePamgen = (params->get("Method","Exodus") == "Pamgen");

  std::vector<std::string> entity_rank_names = stk::mesh::entity_rank_names();

  // eMesh needs "FAMILY_TREE" entity
  if(buildEMesh) {
    entity_rank_names.push_back("FAMILY_TREE");
  }

  if (params->get<bool>("Use Serial Mesh", false) && comm->NumProc() > 1){ 
    // We are parallel but reading a single exodus file
    useSerialMesh = true;

    // Read a single exodus mesh on Proc 0 then rebalance it across the machine
    MPI_Group group_world;
    MPI_Group peZero;
    MPI_Comm peZeroComm;
    MPI_Comm theComm = Albany::getMpiCommFromEpetraComm(*comm);
    int process_rank[1]; // the reader process
    process_rank[0] = 0;
    int my_rank = comm->MyPID();

    //get the group under theComm
    MPI_Comm_group(theComm, &group_world);
    // create the new group. This group includes only processor zero - that is the only processor that reads the file
    MPI_Group_incl(group_world, 1, process_rank, &peZero);
    // create the new communicator - it just contains processor zero
    MPI_Comm_create(theComm, peZero, &peZeroComm);

    mesh_data = new stk::io::StkMeshIoBroker(peZeroComm);
  }
  else {
    mesh_data = new stk::io::StkMeshIoBroker(Albany::getMpiCommFromEpetraComm(*comm));
  }

  // Create input mesh 

  mesh_data->set_rank_name_vector(entity_rank_names);
  std::string mesh_type;
  std::string file_name;
  if (!usePamgen) {
    *out << "Albany_IOSS: Loading STKMesh from Exodus file  "
         << params->get<std::string>("Exodus Input File Name") << std::endl;

    mesh_type = "exodusII";
    file_name = params->get<std::string>("Exodus Input File Name");
  }
  else {
    *out << "Albany_IOSS: Loading STKMesh from Pamgen file  "
         << params->get<std::string>("Pamgen Input File Name") << std::endl;

    mesh_type = "pamgen";
    file_name = params->get<std::string>("Pamgen Input File Name");
  }

  mesh_data->add_mesh_database(file_name, mesh_type, stk::io::READ_MESH);
  mesh_data->create_input_mesh();

  delete metaData;
  metaData = &mesh_data->meta_data();

  // End of creating input mesh

  typedef Teuchos::Array<std::string> StringArray;
  const StringArray additionalNodeSets = params->get("Additional Node Sets", StringArray());
  for (StringArray::const_iterator it = additionalNodeSets.begin(), it_end = additionalNodeSets.end(); it != it_end; ++it) {
    stk::mesh::Part &newNodeSet = metaData->declare_part(*it, stk::topology::NODE_RANK);
    if (!stk::io::is_part_io_part(newNodeSet)) {
      stk::mesh::Field<double> * const distrFactorfield = metaData->get_field<stk::mesh::Field<double> >(stk::topology::NODE_RANK, "distribution_factors");
      stk::mesh::put_field(*distrFactorfield, newNodeSet);
      stk::io::put_io_part_attribute(newNodeSet);
    }
  }

  numDim = metaData->spatial_dimension();

  stk::io::put_io_part_attribute(metaData->universal_part());

  // Set element blocks, side sets and node sets
  const stk::mesh::PartVector & all_parts = metaData->get_parts();
  std::vector<std::string> ssNames;
  std::vector<std::string> nsNames;
  int numEB = 0;

  for (stk::mesh::PartVector::const_iterator i = all_parts.begin();
       i != all_parts.end(); ++i) {

    stk::mesh::Part * const part = *i ;

    if (!stk::mesh::is_auto_declared_part(*part)) {
      if ( part->primary_entity_rank() == stk::topology::ELEMENT_RANK) {

        //*out << "IOSS-STK: Element part \"" << part->name() << "\" found " << std::endl;
        partVec[numEB] = part;
        numEB++;
      }
      else if ( part->primary_entity_rank() == stk::topology::NODE_RANK) {
        //*out << "Mesh has Node Set ID: " << part->name() << std::endl;
        nsPartVec[part->name()]=part;
        nsNames.push_back(part->name());
      }
      else if ( part->primary_entity_rank() == metaData->side_rank()) {
        //print(*out, "Found side_rank entity:\n", *part);
        ssPartVec[part->name()]=part;
      }
    }
  }

  cullSubsetParts(ssNames, ssPartVec); // Eliminate sidesets that are subsets of other sidesets

#if 0
  // for debugging, print out the parts now
  std::map<std::string, stk::mesh::Part*>::iterator it;

  for(it = ssPartVec.begin(); it != ssPartVec.end(); ++it){ // loop over the parts in the map

    // for each part in turn, get the name of parts that are a subset of it

    print(*out, "Found \n", *it->second);
  }
  // end debugging
#endif

  const int cub      = params->get("Cubature Degree",3);
  const int default_cub_rule = static_cast<int>(Intrepid::PL_GAUSS);
  const Intrepid::EIntrepidPLPoly cub_rule = static_cast<Intrepid::EIntrepidPLPoly>(params->get("Cubature Rule",default_cub_rule));
  int worksetSizeMax = params->get("Workset Size",50);

  // Get number of elements per element block using Ioss for use
  // in calculating an upper bound on the worksetSize.
  std::vector<int> el_blocks;
  get_element_block_sizes(*mesh_data, el_blocks);
  TEUCHOS_TEST_FOR_EXCEPT(el_blocks.size() != partVec.size());

  int ebSizeMax =  *std::max_element(el_blocks.begin(), el_blocks.end());
  int worksetSize = this->computeWorksetSize(worksetSizeMax, ebSizeMax);

  // Build a map to get the EB name given the index

  for (int eb=0; eb<numEB; eb++) 

    this->ebNameToIndex[partVec[eb]->name()] = eb;

  // Construct MeshSpecsStruct
  if (!params->get("Separate Evaluators by Element Block",false)) {

    const CellTopologyData& ctd = *metaData->get_cell_topology(*partVec[0]).getCellTopologyData();
    this->meshSpecs[0] = Teuchos::rcp(new Albany::MeshSpecsStruct(ctd, numDim, cub,
                                                                  nsNames, ssNames, worksetSize, partVec[0]->name(), 
                                                                  this->ebNameToIndex, this->interleavedOrdering,cub_rule));

  }
  else {

    *out << "MULTIPLE Elem Block in Ioss: DO worksetSize[eb] max?? " << std::endl; 
    this->allElementBlocksHaveSamePhysics=false;
    this->meshSpecs.resize(numEB);
    for (int eb=0; eb<numEB; eb++) {
      const CellTopologyData& ctd = *metaData->get_cell_topology(*partVec[eb]).getCellTopologyData();
      this->meshSpecs[eb] = Teuchos::rcp(new Albany::MeshSpecsStruct(ctd, numDim, cub,
                                                                     nsNames, ssNames, worksetSize, partVec[eb]->name(), 
                                                                     this->ebNameToIndex, this->interleavedOrdering,cub_rule));
      std::cout << "el_block_size[" << eb << "] = " << el_blocks[eb] << "   name  " << partVec[eb]->name() << std::endl; 
    }

  }

  {
    const Ioss::Region& inputRegion = *(mesh_data->get_input_io_region());
    m_solutionFieldHistoryDepth = inputRegion.get_property("state_count").get_int();
  }
}

Albany::IossSTKMeshStruct::~IossSTKMeshStruct()
{
  metaData = NULL; // prevent deletion
  bulkData = NULL; // prevent deletion
  delete mesh_data;
}

void
Albany::IossSTKMeshStruct::setFieldAndBulkData(
                                               const Teuchos::RCP<const Epetra_Comm>& comm,
                                               const Teuchos::RCP<Teuchos::ParameterList>& params,
                                               const unsigned int neq_,
                                               const AbstractFieldContainer::FieldContainerRequirements& req,
                                               const Teuchos::RCP<Albany::StateInfoStruct>& sis,
                                               const unsigned int worksetSize)
{
  this->SetupFieldData(comm, neq_, req, sis, worksetSize);

  mesh_data->set_bulk_data(*bulkData);

  *out << "IOSS-STK: number of node sets = " << nsPartVec.size() << std::endl;
  *out << "IOSS-STK: number of side sets = " << ssPartVec.size() << std::endl;

  metaData->commit();

  // Restart index to read solution from exodus file.
  int index = params->get("Restart Index",-1); // Default to no restart
  double res_time = params->get<double>("Restart Time",-1.0); // Default to no restart
  Ioss::Region& region = *(mesh_data->get_input_io_region());

  /*
   * The following code block reads a single mesh on PE 0, then distributes the mesh across
   * the other processors. stk_rebalance is used, which requires Zoltan
   *
   * This code is only compiled if ALBANY_MPI and ALBANY_ZOLTAN are true
   */

#ifdef ALBANY_ZOLTAN // rebalance needs Zoltan

  if(useSerialMesh){

    // trick to avoid hanging
    bulkData->modification_begin();

    if(comm->MyPID() == 0){ // read in the mesh on PE 0


      //stk::io::process_mesh_bulk_data(region, *bulkData);
      mesh_data->populate_bulk_data();
      //bulkData = &mesh_data->bulk_data();

      // Read solution from exodus file.
      if (index >= 0) { // User has specified a time step to restart at
        *out << "Restart Index set, reading solution index : " << index << std::endl;
        mesh_data->read_defined_input_fields(index);
        m_restartDataTime = region.get_state_time(index);
        m_hasRestartSolution = true;
      }
      else if (res_time >= 0) { // User has specified a time to restart at
        *out << "Restart solution time set, reading solution time : " << res_time << std::endl;
        mesh_data->read_defined_input_fields(res_time);
        m_restartDataTime = res_time;
        m_hasRestartSolution = true;
      }
      else {

        *out << "Neither restart index or time are set. Not reading solution data from exodus file"<< std::endl;

      }
    }
    else {
      // trick to avoid hanging
      bulkData->modification_begin(); bulkData->modification_begin();
    }

    bulkData->modification_end();

  } // End UseSerialMesh - reading mesh on PE 0

  else 
#endif

    /*
     * The following code block reads a single mesh when Albany is compiled serially, or a
     * Nemspread fileset if ALBANY_MPI is true.
     *
     */

  { // running in Serial or Parallel read from Nemspread files
    mesh_data->populate_bulk_data();
    if (!usePamgen)  {

      // Read solution from exodus file.
      if (index >= 0) { // User has specified a time step to restart at
        *out << "Restart Index set, reading solution index : " << index << std::endl;
        mesh_data->read_defined_input_fields(index);
        m_restartDataTime = region.get_state_time(index);
        m_hasRestartSolution = true;
      }
      else if (res_time >= 0) { // User has specified a time to restart at
        *out << "Restart solution time set, reading solution time : " << res_time << std::endl;
        mesh_data->read_defined_input_fields(res_time);
        m_restartDataTime = res_time;
        m_hasRestartSolution = true;
      }
      else {
        *out << "Restart Index not set. Not reading solution from exodus (" 
             << index << ")"<< std::endl;

      }
    }

    bulkData->modification_end();

  } // End Parallel Read - or running in serial

  if(m_hasRestartSolution){

    Teuchos::Array<std::string> default_field;
    default_field.push_back("solution");
    Teuchos::Array<std::string> restart_fields =
      params->get<Teuchos::Array<std::string> >("Restart Fields", default_field);

    // Get the fields to be used for restart

    // See what state data was initialized from the stk::io request
    // This should be propagated into stk::io
    const Ioss::ElementBlockContainer& elem_blocks = region.get_element_blocks();

    /*
    // Uncomment to print what fields are in the exodus file
    Ioss::NameList exo_fld_names;
    elem_blocks[0]->field_describe(&exo_fld_names);
    for(std::size_t i = 0; i < exo_fld_names.size(); i++){
    *out << "Found field \"" << exo_fld_names[i] << "\" in exodus file" << std::endl; } */

    for (std::size_t i=0; i<sis->size(); i++) { Albany::StateStruct& st = *((*sis)[i]); 
      if(elem_blocks[0]->field_exists(st.name))

        for(std::size_t j = 0; j < restart_fields.size(); j++)

          if(boost::iequals(st.name, restart_fields[j])){

            *out << "Restarting from field \"" << st.name << "\" found in exodus file." << std::endl;
            st.restartDataAvailable = true;
            break;

          }
    }
  }

  // Refine the mesh before starting the simulation if indicated
  uniformRefineMesh(comm);

  // Rebalance the mesh before starting the simulation if indicated
  rebalanceInitialMesh(comm);

  // Build additional mesh connectivity needed for mesh fracture (if indicated)
  computeAddlConnectivity();
}

double
Albany::IossSTKMeshStruct::getSolutionFieldHistoryStamp(int step) const
{
  TEUCHOS_ASSERT(step >= 0 && step < m_solutionFieldHistoryDepth);

  const int index = step + 1; // 1-based step indexing
  const Ioss::Region &  inputRegion = *(mesh_data->get_input_io_region());
  return inputRegion.get_state_time(index);
}

void
Albany::IossSTKMeshStruct::loadSolutionFieldHistory(int step)
{
  TEUCHOS_ASSERT(step >= 0 && step < m_solutionFieldHistoryDepth);

  const int index = step + 1; // 1-based step indexing
  mesh_data->read_defined_input_fields(index);
}

Teuchos::RCP<const Teuchos::ParameterList>
Albany::IossSTKMeshStruct::getValidDiscretizationParameters() const
{
  Teuchos::RCP<Teuchos::ParameterList> validPL =
    this->getValidGenericSTKParameters("Valid IOSS_DiscParams");
  validPL->set<bool>("Periodic BC", false, "Flag to indicate periodic a mesh");
  validPL->set<std::string>("Exodus Input File Name", "", "File Name For Exodus Mesh Input");
  validPL->set<std::string>("Pamgen Input File Name", "", "File Name For Pamgen Mesh Input");
  validPL->set<int>("Restart Index", 1, "Exodus time index to read for inital guess/condition.");
  validPL->set<double>("Restart Time", 1.0, "Exodus solution time to read for inital guess/condition.");

  Teuchos::Array<std::string> emptyStringArray;
  validPL->set<Teuchos::Array<std::string> >("Additional Node Sets", emptyStringArray, "Declare additional node sets not present in the input file");

  return validPL;
}
#endif
