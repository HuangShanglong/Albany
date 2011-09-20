/********************************************************************\
*            Albany, Copyright (2010) Sandia Corporation             *
*                                                                    *
* Notice: This computer software was prepared by Sandia Corporation, *
* hereinafter the Contractor, under Contract DE-AC04-94AL85000 with  *
* the Department of Energy (DOE). All rights in the computer software*
* are reserved by DOE on behalf of the United States Government and  *
* the Contractor as provided in the Contract. You are authorized to  *
* use this computer software for Governmental purposes but it is not *
* to be released or distributed to the public. NEITHER THE GOVERNMENT*
* NOR THE CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR      *
* ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE. This notice    *
* including this sentence must appear on any copies of this software.*
*    Questions to Andy Salinger, agsalin@sandia.gov                  *
\********************************************************************/


#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include "Intrepid_FunctionSpaceTools.hpp"

namespace LCM {

//**********************************************************************
template<typename EvalT, typename Traits>
TotalStress<EvalT, Traits>::
TotalStress(const Teuchos::ParameterList& p) :
  strain           (p.get<std::string>                   ("Strain Name"),
	            p.get<Teuchos::RCP<PHX::DataLayout> >("QP Tensor Data Layout") ),
  elasticModulus   (p.get<std::string>                   ("Elastic Modulus Name"),
	            p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  poissonsRatio    (p.get<std::string>                   ("Poissons Ratio Name"),
	            p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  porePressure    (p.get<std::string>                   ("Pore Pressure Name"),
	            p.get<Teuchos::RCP<PHX::DataLayout> >("QP Scalar Data Layout") ),
  stress           (p.get<std::string>                   ("Total Stress Name"),
	            p.get<Teuchos::RCP<PHX::DataLayout> >("QP Tensor Data Layout") )
{
  // Pull out numQPs and numDims from a Layout
  Teuchos::RCP<PHX::DataLayout> tensor_dl =
    p.get< Teuchos::RCP<PHX::DataLayout> >("QP Tensor Data Layout");
  std::vector<PHX::DataLayout::size_type> dims;
  tensor_dl->dimensions(dims);
  numQPs  = dims[1];
  numDims = dims[2];

  this->addDependentField(strain);
  this->addDependentField(elasticModulus);
  // PoissonRatio not used in 1D stress calc
  if (numDims>1) this->addDependentField(poissonsRatio);
  this->addDependentField(porePressure);

  this->addEvaluatedField(stress);

  this->setName("TotalStress"+PHX::TypeString<EvalT>::value);

}

//**********************************************************************
template<typename EvalT, typename Traits>
void TotalStress<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(stress,fm);
  this->utils.setFieldData(strain,fm);
  this->utils.setFieldData(elasticModulus,fm);
  if (numDims>1) this->utils.setFieldData(poissonsRatio,fm);
  this->utils.setFieldData(porePressure,fm);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void TotalStress<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  ScalarT lambda, mu, bulk, BiotCoeff; // B is the Biot coefficient 1 - K/K(s) where
  const ScalarT Ks = 10.0e5; // This need to be change to variable later...!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  switch (numDims) {
  case 1:
    Intrepid::FunctionSpaceTools::tensorMultiplyDataData<ScalarT>(stress, elasticModulus, strain); // 1D does not work for this problem
    break;
  case 2:
    // Compute Stress (with the plane strain assumption for now)
    for (std::size_t cell=0; cell < workset.numCells; ++cell) {
      for (std::size_t qp=0; qp < numQPs; ++qp) {
	lambda = ( elasticModulus(cell,qp) * poissonsRatio(cell,qp) ) / ( ( 1 + poissonsRatio(cell,qp) ) * ( 1 - 2 * poissonsRatio(cell,qp) ) );
	mu = elasticModulus(cell,qp) / ( 2 * ( 1 + poissonsRatio(cell,qp) ) );
	bulk = lambda + 2.0/3.0*mu;
	BiotCoeff = 1.0 - bulk*(1 - strain(cell,qp,0,0) - strain(cell,qp,1,1))/Ks;// This need to be change later...!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	stress(cell,qp,0,0) = 2.0 * mu * ( strain(cell,qp,0,0) ) + lambda * ( strain(cell,qp,0,0) + strain(cell,qp,1,1) ) - BiotCoeff*porePressure(cell,qp) ;
	stress(cell,qp,1,1) = 2.0 * mu * ( strain(cell,qp,1,1) ) + lambda * ( strain(cell,qp,0,0) + strain(cell,qp,1,1) ) - BiotCoeff*porePressure(cell,qp);
	stress(cell,qp,0,1) = 2.0 * mu * ( strain(cell,qp,0,1) );
	stress(cell,qp,1,0) = stress(cell,qp,0,1); 
      }
    }
    break;
  case 3:
    // Compute Stress
    for (std::size_t cell=0; cell < workset.numCells; ++cell) {
      for (std::size_t qp=0; qp < numQPs; ++qp) {
	lambda = ( elasticModulus(cell,qp) * poissonsRatio(cell,qp) ) / ( ( 1 + poissonsRatio(cell,qp) ) * ( 1 - 2 * poissonsRatio(cell,qp) ) );
	mu = elasticModulus(cell,qp) / ( 2 * ( 1 + poissonsRatio(cell,qp) ) );
	bulk = lambda + 2.0/3.0*mu;
	BiotCoeff = 1.0 - bulk*(1 - strain(cell,qp,0,0) - strain(cell,qp,1,1) - strain(cell,qp,2,2) )/Ks;// This need to be change later...!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	stress(cell,qp,0,0) = 2.0 * mu * ( strain(cell,qp,0,0) ) + lambda * ( strain(cell,qp,0,0) + strain(cell,qp,1,1) + strain(cell,qp,2,2) )
		                  - BiotCoeff*porePressure(cell,qp);
	stress(cell,qp,1,1) = 2.0 * mu * ( strain(cell,qp,1,1) ) + lambda * ( strain(cell,qp,0,0) + strain(cell,qp,1,1) + strain(cell,qp,2,2) )
		                  - BiotCoeff*porePressure(cell,qp);
	stress(cell,qp,2,2) = 2.0 * mu * ( strain(cell,qp,2,2) ) + lambda * ( strain(cell,qp,0,0) + strain(cell,qp,1,1) + strain(cell,qp,2,2) )
		                  - BiotCoeff*porePressure(cell,qp);
	stress(cell,qp,0,1) = 2.0 * mu * ( strain(cell,qp,0,1) );
	stress(cell,qp,1,2) = 2.0 * mu * ( strain(cell,qp,1,2) );
	stress(cell,qp,2,0) = 2.0 * mu * ( strain(cell,qp,2,0) );
	stress(cell,qp,1,0) = stress(cell,qp,0,1); 
	stress(cell,qp,2,1) = stress(cell,qp,1,2); 
	stress(cell,qp,0,2) = stress(cell,qp,2,0); 
      }
    }
    break;
  }
}

//**********************************************************************
}

