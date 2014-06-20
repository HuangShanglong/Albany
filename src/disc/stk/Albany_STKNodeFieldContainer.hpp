//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//


#ifndef ALBANY_STKNODEFIELDCONTAINER_HPP
#define ALBANY_STKNODEFIELDCONTAINER_HPP

#include "Teuchos_RCP.hpp"
#include "Albany_AbstractNodeFieldContainer.hpp"
#include "Albany_StateInfoStruct.hpp"

#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/FieldTraits.hpp>
#include <stk_mesh/base/CoordinateSystems.hpp>

namespace Albany {

/*!
 * \brief Abstract interface for an STK NodeField container
 *
 */

class AbstractSTKNodeFieldContainer : public AbstractNodeFieldContainer {

  public:

    AbstractSTKNodeFieldContainer(){}
    virtual ~AbstractSTKNodeFieldContainer(){}

    virtual void saveField(const Teuchos::RCP<const Epetra_Vector>& block_mv,
            int offset, int blocksize = -1) = 0;
    virtual Albany::MDArray getMDA(const stk::mesh::Bucket& buck) = 0;

};


Teuchos::RCP<Albany::AbstractNodeFieldContainer>
buildSTKNodeField(const std::string& name, const std::vector<int>& dim,
                    stk::mesh::MetaData* metaData,
                    stk::mesh::BulkData* bulkData, const bool output);


  // Helper class for NodeData
  template<typename DataType, unsigned ArrayDim>
  struct NodeData_Traits { };

  template<typename DataType, unsigned ArrayDim, class traits = NodeData_Traits<DataType, ArrayDim> >
  class STKNodeField : public AbstractSTKNodeFieldContainer {

  public:

    //! Type of traits class being used
    typedef traits traits_type;

    //! Define the field type
    typedef typename traits_type::field_type field_type;


    STKNodeField(const std::string& name, const std::vector<int>& dim,
                 stk::mesh::MetaData* metaData, stk::mesh::BulkData* bulkData,
                 const bool output = false);

    virtual ~STKNodeField(){}

    void saveField(const Teuchos::RCP<const Epetra_Vector>& block_mv, int offset, int blocksize = -1);

    Albany::MDArray getMDA(const stk::mesh::Bucket& buck);

  private:

    std::string name;      // Name of data field
    field_type *node_field;  // stk::mesh::field
    std::vector<int> dims;
    stk::mesh::MetaData* metaData;
    stk::mesh::BulkData* bulkData;

  };

// Explicit template definitions in support of the above

  // Node Scalar
  template <typename T>
  struct NodeData_Traits<T, 1> {

    enum { size = 1 }; // Three array dimension tags (Node, Dim, Dim), store type T values
    typedef stk::mesh::Field<T> field_type ;
    static field_type* createField(const std::string& name, const std::vector<int>& dim,
                                   stk::mesh::MetaData* metaData){

        field_type *fld = & metaData->declare_field<field_type>(stk::topology::NODE_RANK, name);
        // Multi-dim order is Fortran Ordering, so reversed here
        stk::mesh::put_field(*fld , metaData->universal_part());

        return fld; // Address is held by stk

    }

    static void saveFieldData(const Teuchos::RCP<const Epetra_Vector>& overlap_node_vec,
                              const stk::mesh::BucketVector& all_elements,
                              field_type *fld, int offset, int blocksize){

      const Epetra_BlockMap& overlap_node_map = overlap_node_vec->Map();
      if(blocksize < 0)
        blocksize = overlap_node_map.ElementSize();

      for(stk::mesh::BucketVector::const_iterator it = all_elements.begin() ; it != all_elements.end() ; ++it) {

        const stk::mesh::Bucket& bucket = **it;
        stk::mesh::BulkData const& bulkData = bucket.mesh();

        double* raw_data = stk::mesh::field_data(*fld, bucket);

        const size_t num_nodes_in_bucket = bucket.size();

        for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

          const int node_gid = bulkData.identifier(bucket[i]) - 1;
          int local_node = overlap_node_map.LID(node_gid);
          int block_start = local_node * blocksize;

          raw_data[i] = (*overlap_node_vec)[block_start + offset];

        }
      }
    }

  };

  // Node Vector
  template <typename T>
  struct NodeData_Traits<T, 2> {

    enum { size = 2 }; // Two array dimension tags (Node, Dim), store type T values
    typedef stk::mesh::Field<T, stk::mesh::Cartesian> field_type ;
    static field_type* createField(const std::string& name, const std::vector<int>& dim,
                                   stk::mesh::MetaData* metaData){

        field_type *fld = & metaData->declare_field<field_type>(stk::topology::NODE_RANK, name);
        // Multi-dim order is Fortran Ordering, so reversed here
        stk::mesh::put_field(*fld , metaData->universal_part(), dim[1]);

        return fld; // Address is held by stk

    }

    static void saveFieldData(const Teuchos::RCP<const Epetra_Vector>& overlap_node_vec,
                              const stk::mesh::BucketVector& all_elements,
                              field_type *fld, int offset, int blocksize){

      const Epetra_BlockMap& overlap_node_map = overlap_node_vec->Map();
      if(blocksize < 0)
        blocksize = overlap_node_map.ElementSize();

      for(stk::mesh::BucketVector::const_iterator it = all_elements.begin() ; it != all_elements.end() ; ++it) {

        const stk::mesh::Bucket& bucket = **it;
        stk::mesh::BulkData const& bulkData = bucket.mesh();

        double* raw_data = stk::mesh::field_data(*fld, bucket);

        const size_t num_vec_components  = stk::mesh::field_scalars_per_entity(*fld, bucket);
        const size_t num_nodes_in_bucket = bucket.size();

        for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

          const int node_gid = bulkData.identifier(bucket[i]) - 1;
          int local_node = overlap_node_map.LID(node_gid);
          int block_start = local_node * blocksize;

          for(std::size_t j = 0; j < num_vec_components; j++){

            raw_data[i*num_vec_components + j]= (*overlap_node_vec)[block_start + offset + j];

          }
        }
      }
    }

  };

  // Node Tensor
  template <typename T>
  struct NodeData_Traits<T, 3> {

    enum { size = 3 }; // Three array dimension tags (Node, Dim, Dim), store type T values
    typedef stk::mesh::Field<T, stk::mesh::Cartesian, stk::mesh::Cartesian> field_type ;
    static field_type* createField(const std::string& name, const std::vector<int>& dim,
                                   stk::mesh::MetaData* metaData){

        field_type *fld = & metaData->declare_field<field_type>(stk::topology::NODE_RANK, name);
        // Multi-dim order is Fortran Ordering, so reversed here
        stk::mesh::put_field(*fld , metaData->universal_part(), dim[2], dim[1]);

        return fld; // Address is held by stk

    }

    static void saveFieldData(const Teuchos::RCP<const Epetra_Vector>& overlap_node_vec,
                              const stk::mesh::BucketVector& all_elements,
                              field_type *fld, int offset, int blocksize){

      const Epetra_BlockMap& overlap_node_map = overlap_node_vec->Map();
      if(blocksize < 0)
        blocksize = overlap_node_map.ElementSize();

      for(stk::mesh::BucketVector::const_iterator it = all_elements.begin() ; it != all_elements.end() ; ++it) {

        const stk::mesh::Bucket& bucket = **it;
        stk::mesh::BulkData const& bulkData = bucket.mesh();

        double* raw_data = stk::mesh::field_data(*fld, bucket);

        const size_t num_i_components = stk::mesh::Cartesian::Size;
        const size_t num_j_components = stk::mesh::Cartesian::Size;
        const size_t num_nodes_in_bucket = bucket.size();

        for(std::size_t i = 0; i < num_nodes_in_bucket; i++)  {

          const int node_gid = bulkData.identifier(bucket[i]) - 1;
          int local_node = overlap_node_map.LID(node_gid);
          int block_start = local_node * blocksize;

          for(std::size_t j = 0; j < num_j_components; j++)
            for(std::size_t k = 0; k < num_i_components; k++)

              raw_data[i*num_i_components*num_j_components + j*num_i_components + k] =
                (*overlap_node_vec)[block_start + offset + j*num_i_components + k];

        }
      }
    }

  };


}

// Define macro for explicit template instantiation
#define STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS_SCAL(name, type) \
  template class name<type, 1>;
#define STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS_VEC(name, type) \
  template class name<type, 2>;
#define STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS_TENS(name, type) \
  template class name<type, 3>;


#define STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS(name) \
  STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS_SCAL(name, double) \
  STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS_VEC(name, double) \
  STKNODEFIELDCONTAINER_INSTANTIATE_TEMPLATE_CLASS_TENS(name, double)

#endif // ALBANY_STKNODEFIELDCONTAINER_HPP
