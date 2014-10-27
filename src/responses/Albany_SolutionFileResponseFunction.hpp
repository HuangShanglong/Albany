//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef ALBANY_SOLUTIONFILERESPONSEFUNCTION_HPP
#define ALBANY_SOLUTIONFILERESPONSEFUNCTION_HPP

#include "Albany_SamplingBasedScalarResponseFunction.hpp"

namespace Albany {

  /*!
   * \brief Response function representing the difference from a stored vector on disk
   */
  template<class VectorNorm>
  class SolutionFileResponseFunction : 
    public SamplingBasedScalarResponseFunction {
  public:
  
    //! Default constructor
    SolutionFileResponseFunction(const Teuchos::RCP<const Epetra_Comm>& comm);

    //! Destructor
    virtual ~SolutionFileResponseFunction();

    //! Get the number of responses
    virtual unsigned int numResponses() const;

    //! Evaluate responses
    virtual void 
    evaluateResponse(const double current_time,
		     const Epetra_Vector* xdot,
		     const Epetra_Vector* xdotdot,
		     const Epetra_Vector& x,
		     const Teuchos::Array<ParamVec>& p,
		     Epetra_Vector& g);

    //! Evaluate tangent = dg/dx*dx/dp + dg/dxdot*dxdot/dp + dg/dp
    virtual void 
    evaluateTangent(const double alpha, 
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
		    Epetra_MultiVector* gp);

    //! Evaluate gradient = dg/dx, dg/dxdot, dg/dp
    virtual void 
    evaluateGradient(const double current_time,
		     const Epetra_Vector* xdot,
		     const Epetra_Vector* xdotdot,
		     const Epetra_Vector& x,
		     const Teuchos::Array<ParamVec>& p,
		     ParamVec* deriv_p,
		     Epetra_Vector* g,
		     Epetra_MultiVector* dg_dx,
		     Epetra_MultiVector* dg_dxdot,
		     Epetra_MultiVector* dg_dxdotdot,
		     Epetra_MultiVector* dg_dp);

    //! Evaluate distributed parameter derivative dg/dp
    virtual void
    evaluateDistParamDeriv(
        const double current_time,
        const Epetra_Vector* xdot,
        const Epetra_Vector* xdotdot,
        const Epetra_Vector& x,
        const Teuchos::Array<ParamVec>& param_array,
        const std::string& dist_param_name,
        Epetra_MultiVector* dg_dp);

  private:

    //! Private to prohibit copying
    SolutionFileResponseFunction(const SolutionFileResponseFunction&);
    
    //! Private to prohibit copying
    SolutionFileResponseFunction& operator=(const SolutionFileResponseFunction&);

    //! Reference Vector
    Epetra_Vector* RefSoln;

    bool solutionLoaded;

    //! Basic idea borrowed from EpetraExt - TO DO: put it back there?
    int MatrixMarketFileToVector( const char *filename, const Epetra_BlockMap & map, Epetra_Vector * & A);
    int MatrixMarketFileToMultiVector( const char *filename, const Epetra_BlockMap & map, Epetra_MultiVector * & A);

  };

//	namespace SolutionFileResponseFunction {
	
	  struct NormTwo {
	
	    double Norm(const Epetra_Vector& vec){ double norm; vec.Norm2(&norm); return norm * norm;}
	
	  };
	
	  struct NormInf {
	
	    double Norm(const Epetra_Vector& vec){ double norm; vec.NormInf(&norm); return norm;}
	
	  };
//	}

}

// Define macro for explicit template instantiation
#define SOLFILERESP_INSTANTIATE_TEMPLATE_CLASS_NORMTWO(name) \
  template class name<Albany::NormTwo>;
#define SOLFILERESP_INSTANTIATE_TEMPLATE_CLASS_NORMINF(name) \
  template class name<Albany::NormInf>;

#define SOLFILERESP_INSTANTIATE_TEMPLATE_CLASS(name) \
  SOLFILERESP_INSTANTIATE_TEMPLATE_CLASS_NORMTWO(name) \
  SOLFILERESP_INSTANTIATE_TEMPLATE_CLASS_NORMINF(name)

#endif // ALBANY_SOLUTIONFILERESPONSEFUNCTION_HPP
