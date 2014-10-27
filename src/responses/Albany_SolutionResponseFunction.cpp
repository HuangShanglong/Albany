//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Albany_SolutionResponseFunction.hpp"
#include "Epetra_CrsMatrix.h"
#include <algorithm>

Albany::SolutionResponseFunction::
SolutionResponseFunction(
  const Teuchos::RCP<Albany::Application>& application_,
  Teuchos::ParameterList& responseParams) : 
  application(application_)
{
  // Build list of DOFs we want to keep
  // This should be replaced by DOF names eventually
  int numDOF = application->getProblem()->numEquations();
  if (responseParams.isType< Teuchos::Array<int> >("Keep DOF Indices")) {
    Teuchos::Array<int> dofs = 
      responseParams.get< Teuchos::Array<int> >("Keep DOF Indices");
    keepDOF = Teuchos::Array<int>(numDOF, 0);
    for (int i=0; i<dofs.size(); i++)
      keepDOF[dofs[i]] = 1;
  }
  else {
    keepDOF = Teuchos::Array<int>(numDOF, 1);
  }
}

void
Albany::SolutionResponseFunction::
setup()
{
  // Build culled map and importer
  Teuchos::RCP<const Epetra_Map> x_map = application->getMap();
  culled_map = buildCulledMap(*x_map, keepDOF);
  importer = Teuchos::rcp(new Epetra_Import(*culled_map, *x_map));

  // Create graph for gradient operator -- diagonal matrix
  gradient_graph = 
    Teuchos::rcp(new Epetra_CrsGraph(Copy, *culled_map, 1, true));
  for (int i=0; i<culled_map->NumMyElements(); i++) {
    int row = culled_map->GID(i);
    gradient_graph->InsertGlobalIndices(row, 1, &row);
  }
  gradient_graph->FillComplete();
  gradient_graph->OptimizeStorage();
}

Albany::SolutionResponseFunction::
~SolutionResponseFunction()
{
}

Teuchos::RCP<const Epetra_Map>
Albany::SolutionResponseFunction::
responseMap() const
{
  return culled_map;
}

Teuchos::RCP<Epetra_Operator>
Albany::SolutionResponseFunction::
createGradientOp() const
{
  Teuchos::RCP<Epetra_CrsMatrix> G =
    Teuchos::rcp(new Epetra_CrsMatrix(Copy, *gradient_graph));
  G->FillComplete();
  return G;
}

void
Albany::SolutionResponseFunction::
evaluateResponse(const double current_time,
		 const Epetra_Vector* xdot,
		 const Epetra_Vector* xdotdot,
		 const Epetra_Vector& x,
		 const Teuchos::Array<ParamVec>& p,
		 Epetra_Vector& g)
{
  cullSolution(x, g);
}

void
Albany::SolutionResponseFunction::
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
		Epetra_MultiVector* gp)
{
  if (g)
    cullSolution(x, *g);

  if (gx) {
    gx->PutScalar(0.0);
    if (Vx) {
      cullSolution(*Vx, *gx);
      gx->Scale(beta);
    }
  }

  if (gp)
    gp->PutScalar(0.0);
}

void
Albany::SolutionResponseFunction::
evaluateGradient(const double current_time,
		 const Epetra_Vector* xdot,
		 const Epetra_Vector* xdotdot,
		 const Epetra_Vector& x,
		 const Teuchos::Array<ParamVec>& p,
		 ParamVec* deriv_p,
		 Epetra_Vector* g,
		 Epetra_Operator* dg_dx,
		 Epetra_Operator* dg_dxdot,
		 Epetra_Operator* dg_dxdotdot,
		 Epetra_MultiVector* dg_dp)
{
  if (g)
    cullSolution(x, *g);

  if (dg_dx) {
    Epetra_CrsMatrix *dg_dx_crs = dynamic_cast<Epetra_CrsMatrix*>(dg_dx);
    TEUCHOS_TEST_FOR_EXCEPT(dg_dx_crs == NULL);
    dg_dx_crs->PutScalar(1.0); // matrix only stores the diagonal
  }

  if (dg_dxdot) {
    Epetra_CrsMatrix *dg_dxdot_crs = dynamic_cast<Epetra_CrsMatrix*>(dg_dxdot);
    TEUCHOS_TEST_FOR_EXCEPT(dg_dxdot_crs == NULL);
    dg_dxdot_crs->PutScalar(0.0); // matrix only stores the diagonal
  }
  if (dg_dxdotdot) {
    Epetra_CrsMatrix *dg_dxdotdot_crs = dynamic_cast<Epetra_CrsMatrix*>(dg_dxdotdot);
    TEUCHOS_TEST_FOR_EXCEPT(dg_dxdotdot_crs == NULL);
    dg_dxdotdot_crs->PutScalar(0.0); // matrix only stores the diagonal
  }

  if (dg_dp)
    dg_dp->PutScalar(0.0);
}

void
Albany::SolutionResponseFunction::
evaluateDistParamDeriv(
      const double current_time,
      const Epetra_Vector* xdot,
      const Epetra_Vector* xdotdot,
      const Epetra_Vector& x,
      const Teuchos::Array<ParamVec>& param_array,
      const std::string& dist_param_name,
      Epetra_MultiVector* dg_dp)
{
  if (dg_dp)
    dg_dp->PutScalar(0.0);
}

#ifdef ALBANY_SG_MP
void
Albany::SolutionResponseFunction::
init_sg(
  const Teuchos::RCP<const Stokhos::OrthogPolyBasis<int,double> >& basis,
  const Teuchos::RCP<const Stokhos::Quadrature<int,double> >& quad,
  const Teuchos::RCP<Stokhos::OrthogPolyExpansion<int,double> >& expansion,
  const Teuchos::RCP<const EpetraExt::MultiComm>& multiComm)
{
}

void
Albany::SolutionResponseFunction::
evaluateSGResponse(const double curr_time,
		   const Stokhos::EpetraVectorOrthogPoly* sg_xdot,
		   const Stokhos::EpetraVectorOrthogPoly* sg_xdotdot,
		   const Stokhos::EpetraVectorOrthogPoly& sg_x,
		   const Teuchos::Array<ParamVec>& p,
		   const Teuchos::Array<int>& sg_p_index,
		   const Teuchos::Array< Teuchos::Array<SGType> >& sg_p_vals,
		   Stokhos::EpetraVectorOrthogPoly& sg_g)
{
  // Note that by doing the culling this way, instead of importing into
  // sg_g directly using a product importer, it doesn't really matter that
  // the product maps between sg_x and sg_g aren't consistent
  for (int i=0; i<sg_g.size(); i++)
    cullSolution(sg_x[i], sg_g[i]);
}

void
Albany::SolutionResponseFunction::
evaluateSGTangent(const double alpha, 
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
		  Stokhos::EpetraMultiVectorOrthogPoly* sg_gp)
{
  if (sg_g)
    for (int i=0; i<sg_g->size(); i++)
      cullSolution(sg_x[i], (*sg_g)[i]);

  if (sg_JV) {
    sg_JV->init(0.0);
    if (Vx) {
      cullSolution(*Vx, (*sg_JV)[0]);
      (*sg_JV)[0].Scale(beta);
    }
  }

  if (sg_gp)
    sg_gp->init(0.0);
}

void
Albany::SolutionResponseFunction::
evaluateSGGradient(const double current_time,
		   const Stokhos::EpetraVectorOrthogPoly* sg_xdot,
		   const Stokhos::EpetraVectorOrthogPoly* sg_xdotdot,
		   const Stokhos::EpetraVectorOrthogPoly& sg_x,
		   const Teuchos::Array<ParamVec>& p,
		   const Teuchos::Array<int>& sg_p_index,
		   const Teuchos::Array< Teuchos::Array<SGType> >& sg_p_vals,
		   ParamVec* deriv_p,
		   Stokhos::EpetraVectorOrthogPoly* sg_g,
		   Stokhos::EpetraOperatorOrthogPoly* sg_dg_dx,
		   Stokhos::EpetraOperatorOrthogPoly* sg_dg_dxdot,
		   Stokhos::EpetraOperatorOrthogPoly* sg_dg_dxdotdot,
		   Stokhos::EpetraMultiVectorOrthogPoly* sg_dg_dp)
{
  if (sg_g)
    for (int i=0; i<sg_g->size(); i++)
      cullSolution(sg_x[i], (*sg_g)[i]);

  if (sg_dg_dx) {
    sg_dg_dx->init(0.0);
    Teuchos::RCP<Epetra_CrsMatrix> dg_dx_crs = 
      Teuchos::rcp_dynamic_cast<Epetra_CrsMatrix>(sg_dg_dx->getCoeffPtr(0),
						  true);
    dg_dx_crs->PutScalar(1.0); // matrix only stores the diagonal
  }

  if (sg_dg_dxdot) {
    sg_dg_dxdot->init(0.0);
  }
  if (sg_dg_dxdotdot) {
    sg_dg_dxdotdot->init(0.0);
  }

  if (sg_dg_dp)
    sg_dg_dp->init(0.0);
}

void
Albany::SolutionResponseFunction::
evaluateMPResponse(const double curr_time,
		   const Stokhos::ProductEpetraVector* mp_xdot,
		   const Stokhos::ProductEpetraVector* mp_xdotdot,
		   const Stokhos::ProductEpetraVector& mp_x,
		   const Teuchos::Array<ParamVec>& p,
		   const Teuchos::Array<int>& mp_p_index,
		   const Teuchos::Array< Teuchos::Array<MPType> >& mp_p_vals,
		   Stokhos::ProductEpetraVector& mp_g)
{
  for (int i=0; i<mp_g.size(); i++)
    cullSolution(mp_x[i], mp_g[i]);
}


void
Albany::SolutionResponseFunction::
evaluateMPTangent(const double alpha, 
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
		  Stokhos::ProductEpetraMultiVector* mp_gp)
{
  if (mp_g)
    for (int i=0; i<mp_g->size(); i++)
      cullSolution(mp_x[i], (*mp_g)[i]);

  if (mp_JV) {
    mp_JV->init(0.0);
    if (Vx) {
      for (int i=0; i<mp_JV->size(); i++) {
	cullSolution(*Vx, (*mp_JV)[i]);
	(*mp_JV)[i].Scale(beta);
      }
    }
  }

  if (mp_gp)
    mp_gp->init(0.0);
}

void
Albany::SolutionResponseFunction::
evaluateMPGradient(const double current_time,
		   const Stokhos::ProductEpetraVector* mp_xdot,
		   const Stokhos::ProductEpetraVector* mp_xdotdot,
		   const Stokhos::ProductEpetraVector& mp_x,
		   const Teuchos::Array<ParamVec>& p,
		   const Teuchos::Array<int>& mp_p_index,
		   const Teuchos::Array< Teuchos::Array<MPType> >& mp_p_vals,
		   ParamVec* deriv_p,
		   Stokhos::ProductEpetraVector* mp_g,
		   Stokhos::ProductEpetraOperator* mp_dg_dx,
		   Stokhos::ProductEpetraOperator* mp_dg_dxdot,
		   Stokhos::ProductEpetraOperator* mp_dg_dxdotdot,
		   Stokhos::ProductEpetraMultiVector* mp_dg_dp)
{
  if (mp_g)
    for (int i=0; i<mp_g->size(); i++)
      cullSolution(mp_x[i], (*mp_g)[i]);

  if (mp_dg_dx) {
    for (int i=0; i<mp_dg_dx->size(); i++) {
      Teuchos::RCP<Epetra_CrsMatrix> dg_dx_crs = 
	Teuchos::rcp_dynamic_cast<Epetra_CrsMatrix>(mp_dg_dx->getCoeffPtr(i),
						    true);
      dg_dx_crs->PutScalar(1.0); // matrix only stores the diagonal
    }
  }

  if (mp_dg_dxdot) {
    mp_dg_dxdot->init(0.0);
  }
  if (mp_dg_dxdotdot) {
    mp_dg_dxdotdot->init(0.0);
  }

  if (mp_dg_dp)
    mp_dg_dp->init(0.0);
}
#endif //ALBANY_SG_MP

Teuchos::RCP<Epetra_Map> 
Albany::SolutionResponseFunction::
buildCulledMap(const Epetra_Map& x_map, 
	       const Teuchos::Array<int>& keepDOF) const 
{
  int numKeepDOF = std::accumulate(keepDOF.begin(), keepDOF.end(), 0);
  int Neqns = keepDOF.size();
  int N = x_map.NumMyElements(); // x_map is map for solution vector

  TEUCHOS_ASSERT( !(N % Neqns) ); // Assume that all the equations for
                                  // a given node are on the assigned
                                  // processor. I.e. need to ensure
                                  // that N is exactly Neqns-divisible

  int nnodes = N / Neqns;          // number of fem nodes
  int N_new = nnodes * numKeepDOF; // length of local x_new   

  int *gids = x_map.MyGlobalElements(); // Fill local x_map into gids array
  Teuchos::Array<int> gids_new(N_new);
  int idx = 0;
  for ( int inode = 0; inode < N/Neqns ; ++inode) // For every node 
    for ( int ieqn = 0; ieqn < Neqns; ++ieqn )  // Check every dof on the node
      if ( keepDOF[ieqn] == 1 )  // then want to keep this dof
	gids_new[idx++] = gids[(inode*Neqns)+ieqn];
  // end cull
  
  Teuchos::RCP<Epetra_Map> x_new_map = 
    Teuchos::rcp( new Epetra_Map( -1, N_new, &gids_new[0], 0, x_map.Comm() ) );
  
  return x_new_map;
}

void
Albany::SolutionResponseFunction::
cullSolution(const Epetra_MultiVector& x, Epetra_MultiVector& x_culled) const
{
  x_culled.Import(x, *importer, Insert);
}
