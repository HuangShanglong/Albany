//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef ALBANY_ABSTRACTRESPONSEFUNCTION_HPP
#define ALBANY_ABSTRACTRESPONSEFUNCTION_HPP

#include "Teuchos_Array.hpp"
#include "Teuchos_RCP.hpp"
#include "Epetra_Map.h"
#include "Epetra_Vector.h"
#include "Epetra_MultiVector.h"
#include "EpetraExt_ModelEvaluator.h"
#include "Stokhos_OrthogPolyBasis.hpp"
#include "Stokhos_OrthogPolyExpansion.hpp"
#include "Stokhos_Quadrature.hpp"
#include "EpetraExt_MultiComm.h"
#include "Stokhos_EpetraVectorOrthogPoly.hpp"
#include "Stokhos_EpetraMultiVectorOrthogPoly.hpp"
#include "Stokhos_ProductEpetraVector.hpp"
#include "Stokhos_ProductEpetraMultiVector.hpp"
#include "PHAL_AlbanyTraits.hpp"

namespace Albany {

  /*!
   * \brief Abstract interface for representing a response function
   */
  class AbstractResponseFunction {
  public:
  
    //! Default constructor
    AbstractResponseFunction() {};

    //! Destructor
    virtual ~AbstractResponseFunction() {};

    //! Setup response function
    virtual void setup() = 0;

    //! Get the map associate with this response
    virtual Teuchos::RCP<const Epetra_Map> responseMap() const = 0;

    /*! 
     * \brief Is this response function "scalar" valued, i.e., has a replicated
     * local response map.
     */
    virtual bool isScalarResponse() const = 0;

    //! Create operator for gradient (e.g., dg/dx)
    virtual Teuchos::RCP<Epetra_Operator> createGradientOp() const = 0;

    //! \name Deterministic evaluation functions
    //@{

    //! Evaluate responses
    virtual void evaluateResponse(
      const double current_time,
      const Epetra_Vector* xdot,
      const Epetra_Vector* xdotdot,
      const Epetra_Vector& x,
      const Teuchos::Array<ParamVec>& p,
      Epetra_Vector& g) = 0;

    //! Evaluate tangent = dg/dx*dx/dp + dg/dxdot*dxdot/dp + dg/dp
    virtual void evaluateTangent(
      const double alpha, 
      const double beta,
      const double omega,
      const double current_time,
      bool sum_derivs,
      const Epetra_Vector* xdot,
      const Epetra_Vector* xdotdot,
      const Epetra_Vector& x,
      const Teuchos::Array<ParamVec>& p,
      ParamVec* deriv_p,
      const Epetra_MultiVector* Vxdot,
      const Epetra_MultiVector* Vxdotdot,
      const Epetra_MultiVector* Vx,
      const Epetra_MultiVector* Vp,
      Epetra_Vector* g,
      Epetra_MultiVector* gx,
      Epetra_MultiVector* gp) = 0;

    //! Evaluate derivative dg/dx, dg/dxdot, dg/dp
    virtual void evaluateDerivative(
      const double current_time,
      const Epetra_Vector* xdot,
      const Epetra_Vector* xdotdot,
      const Epetra_Vector& x,
      const Teuchos::Array<ParamVec>& p,
      ParamVec* deriv_p,
      Epetra_Vector* g,
      const EpetraExt::ModelEvaluator::Derivative& dg_dx,
      const EpetraExt::ModelEvaluator::Derivative& dg_dxdot,
      const EpetraExt::ModelEvaluator::Derivative& dg_dxdotdot,
      const EpetraExt::ModelEvaluator::Derivative& dg_dp) = 0;

    //! Evaluate distributed parameter derivative dg/dp
    virtual void evaluateDistParamDeriv(
      const double current_time,
      const Epetra_Vector* xdot,
      const Epetra_Vector* xdotdot,
      const Epetra_Vector& x,
      const Teuchos::Array<ParamVec>& param_array,
      const std::string& dist_param_name,
      Epetra_MultiVector*  dg_dp) = 0;

    //@}

    //! \name Stochastic Galerkin evaluation functions
    //@{

#ifdef ALBANY_SG_MP
    //! Intialize stochastic Galerkin method
    virtual void init_sg(
      const Teuchos::RCP<const Stokhos::OrthogPolyBasis<int,double> >& basis,
      const Teuchos::RCP<const Stokhos::Quadrature<int,double> >& quad,
      const Teuchos::RCP<Stokhos::OrthogPolyExpansion<int,double> >& expansion,
      const Teuchos::RCP<const EpetraExt::MultiComm>& multiComm) = 0;

    //! Evaluate stochastic Galerkin response
    virtual void evaluateSGResponse(
      const double curr_time,
      const Stokhos::EpetraVectorOrthogPoly* sg_xdot,
      const Stokhos::EpetraVectorOrthogPoly* sg_xdotdot,
      const Stokhos::EpetraVectorOrthogPoly& sg_x,
      const Teuchos::Array<ParamVec>& p,
      const Teuchos::Array<int>& sg_p_index,
      const Teuchos::Array< Teuchos::Array<SGType> >& sg_p_vals,
      Stokhos::EpetraVectorOrthogPoly& sg_g) = 0;

    //! Evaluate stochastic Galerkin tangent
    virtual void evaluateSGTangent(
      const double alpha, 
      const double beta, 
      const double omega, 
      const double current_time,
      bool sum_derivs,
      const Stokhos::EpetraVectorOrthogPoly* sg_xdot,
      const Stokhos::EpetraVectorOrthogPoly* sg_xdotdot,
      const Stokhos::EpetraVectorOrthogPoly& sg_x,
      const Teuchos::Array<ParamVec>& p,
      const Teuchos::Array<int>& sg_p_index,
      const Teuchos::Array< Teuchos::Array<SGType> >& sg_p_vals,
      ParamVec* deriv_p,
      const Epetra_MultiVector* Vx,
      const Epetra_MultiVector* Vxdot,
      const Epetra_MultiVector* Vxdotdot,
      const Epetra_MultiVector* Vp,
      Stokhos::EpetraVectorOrthogPoly* sg_g,
      Stokhos::EpetraMultiVectorOrthogPoly* sg_JV,
      Stokhos::EpetraMultiVectorOrthogPoly* sg_gp) = 0;

    //! Evaluate stochastic Galerkin derivative
    virtual void evaluateSGDerivative(
      const double current_time,
      const Stokhos::EpetraVectorOrthogPoly* sg_xdot,
      const Stokhos::EpetraVectorOrthogPoly* sg_xdotdot,
      const Stokhos::EpetraVectorOrthogPoly& sg_x,
      const Teuchos::Array<ParamVec>& p,
      const Teuchos::Array<int>& sg_p_index,
      const Teuchos::Array< Teuchos::Array<SGType> >& sg_p_vals,
      ParamVec* deriv_p,
      Stokhos::EpetraVectorOrthogPoly* sg_g,
      const EpetraExt::ModelEvaluator::SGDerivative& sg_dg_dx,
      const EpetraExt::ModelEvaluator::SGDerivative& sg_dg_dxdot,
      const EpetraExt::ModelEvaluator::SGDerivative& sg_dg_dxdotdot,
      const EpetraExt::ModelEvaluator::SGDerivative& sg_dg_dp) = 0;
#endif //ALBANY_SG_MP

    //@}

    //! \name Multi-point evaluation functions
    //@{

#ifdef ALBANY_SG_MP
    //! Evaluate multi-point response functions
    virtual void evaluateMPResponse(
      const double curr_time,
      const Stokhos::ProductEpetraVector* mp_xdot,
      const Stokhos::ProductEpetraVector* mp_xdotdot,
      const Stokhos::ProductEpetraVector& mp_x,
      const Teuchos::Array<ParamVec>& p,
      const Teuchos::Array<int>& mp_p_index,
      const Teuchos::Array< Teuchos::Array<MPType> >& mp_p_vals,
      Stokhos::ProductEpetraVector& mp_g) = 0;

    //! Evaluate multi-point tangent
    virtual void evaluateMPTangent(
      const double alpha, 
      const double beta, 
      const double omega, 
      const double current_time,
      bool sum_derivs,
      const Stokhos::ProductEpetraVector* mp_xdot,
      const Stokhos::ProductEpetraVector* mp_xdotdot,
      const Stokhos::ProductEpetraVector& mp_x,
      const Teuchos::Array<ParamVec>& p,
      const Teuchos::Array<int>& mp_p_index,
      const Teuchos::Array< Teuchos::Array<MPType> >& mp_p_vals,
      ParamVec* deriv_p,
      const Epetra_MultiVector* Vx,
      const Epetra_MultiVector* Vxdot,
      const Epetra_MultiVector* Vxdotdot,
      const Epetra_MultiVector* Vp,
      Stokhos::ProductEpetraVector* mp_g,
      Stokhos::ProductEpetraMultiVector* mp_JV,
      Stokhos::ProductEpetraMultiVector* mp_gp) = 0;

    //! Evaluate multi-point derivative
    virtual void evaluateMPDerivative(
      const double current_time,
      const Stokhos::ProductEpetraVector* mp_xdot,
      const Stokhos::ProductEpetraVector* mp_xdotdot,
      const Stokhos::ProductEpetraVector& mp_x,
      const Teuchos::Array<ParamVec>& p,
      const Teuchos::Array<int>& mp_p_index,
      const Teuchos::Array< Teuchos::Array<MPType> >& mp_p_vals,
      ParamVec* deriv_p,
      Stokhos::ProductEpetraVector* mp_g,
      const EpetraExt::ModelEvaluator::MPDerivative& mp_dg_dx,
      const EpetraExt::ModelEvaluator::MPDerivative& mp_dg_dxdot,
      const EpetraExt::ModelEvaluator::MPDerivative& mp_dg_dxdotdot,
      const EpetraExt::ModelEvaluator::MPDerivative& mp_dg_dp) = 0;
#endif //ALBANY_SG_MP

    //@}

  private:

    //! Private to prohibit copying
    AbstractResponseFunction(const AbstractResponseFunction&);
    
    //! Private to prohibit copying
    AbstractResponseFunction& operator=(const AbstractResponseFunction&);

  };

}

#endif // ALBANY_ABSTRACTRESPONSEFUNCTION_HPP
