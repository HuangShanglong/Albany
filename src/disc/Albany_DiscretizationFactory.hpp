//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef ALBANY_DISCRETIZATIONFACTORY_HPP
#define ALBANY_DISCRETIZATIONFACTORY_HPP

#include <vector>

#include "Teuchos_ParameterList.hpp"
#include "Teuchos_RCP.hpp"
#include "Epetra_Comm.h"

#include "Albany_AbstractDiscretization.hpp"
#include "Albany_AbstractMeshStruct.hpp"
#include "Albany_AbstractFieldContainer.hpp"

#include "Piro_NullSpaceUtils.hpp"

#ifdef ALBANY_CUTR
#include "CUTR_CubitMeshMover.hpp"
#endif

namespace Albany {

/*!
 * \brief A factory class to instantiate AbstractDiscretization objects
 */
class DiscretizationFactory {
  public:

    //! Default constructor
    DiscretizationFactory(
      const Teuchos::RCP<Teuchos::ParameterList>& topLevelParams,
      const Teuchos::RCP<const Epetra_Comm>& epetra_comm
    );

    //! Destructor
    ~DiscretizationFactory() {}

    //! Method to inject cubit dependence.
#ifdef ALBANY_CUTR
    void setMeshMover(const Teuchos::RCP<CUTR::CubitMeshMover>& meshMover_);
#endif

    Teuchos::RCP<Albany::AbstractMeshStruct> getMeshStruct() {
      return meshStruct;
    }

    Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> > createMeshSpecs();

    Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> > createMeshSpecs(Teuchos::RCP<Albany::AbstractMeshStruct> mesh);

    Teuchos::RCP<Albany::AbstractDiscretization>
    createDiscretization(unsigned int num_equations,
                         const Teuchos::RCP<Albany::StateInfoStruct>& sis,
                         const AbstractFieldContainer::FieldContainerRequirements& req,
                         const Teuchos::RCP<Piro::MLRigidBodyModes>& rigidBodyModes = Teuchos::null);

    void
    setupInternalMeshStruct(
      unsigned int neq,
      const Teuchos::RCP<Albany::StateInfoStruct>& sis,
      const AbstractFieldContainer::FieldContainerRequirements& req);

    Teuchos::RCP<Albany::AbstractDiscretization> createDiscretizationFromInternalMeshStruct(
      const Teuchos::RCP<Piro::MLRigidBodyModes>& rigidBodyModes);


  private:

    //! Private to prohibit copying
    DiscretizationFactory(const DiscretizationFactory&);

    //! Private to prohibit copying
    DiscretizationFactory& operator=(const DiscretizationFactory&);

  protected:

    //! Parameter list specifying what element to create
    Teuchos::RCP<Teuchos::ParameterList> discParams;

    //! Parameter list specifying adaptation parameters (null if problem isn't adaptive)
    Teuchos::RCP<Teuchos::ParameterList> adaptParams;

    //! Parameter list specifying solver parameters
    Teuchos::RCP<Teuchos::ParameterList> piroParams;

    //! Parameter list specifying parameters for Catalyst
    Teuchos::RCP<Teuchos::ParameterList> catalystParams;

    Teuchos::RCP<const Epetra_Comm> epetra_comm;

#ifdef ALBANY_CUTR
    Teuchos::RCP<CUTR::CubitMeshMover> meshMover;
#endif

    Teuchos::RCP<Albany::AbstractMeshStruct> meshStruct;

};

}

#endif // ALBANY_DISCRETIZATIONFACTORY_HPP
