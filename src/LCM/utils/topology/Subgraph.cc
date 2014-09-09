//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//
#include "Subgraph.h"
#include "Topology.h"
#include "Topology_Utils.h"

namespace LCM {

//
// Create a subgraph given a vertex list and an edge list.
//
Subgraph::Subgraph(
    Topology & topology,
    std::set<EntityKey>::iterator first_vertex,
    std::set<EntityKey>::iterator last_vertex,
    std::set<stkEdge>::iterator first_edge,
    std::set<stkEdge>::iterator last_edge) :
    topology_(topology)
{
  // Insert vertices and create the vertex map
  for (std::set<EntityKey>::iterator vertex_iterator = first_vertex;
      vertex_iterator != last_vertex;
      ++vertex_iterator) {

    // get global vertex
    EntityKey
    global_vertex = *vertex_iterator;

    // get entity
    Entity
    vertex_entity = getBulkData()->get_entity(global_vertex);

    // get entity rank
    stk::mesh::EntityRank
    vertex_rank = getBulkData()->entity_rank(vertex_entity);

    // create new local vertex
    Vertex
    local_vertex = boost::add_vertex(*this);

    std::pair<Vertex, EntityKey>
    local_to_global = std::make_pair(local_vertex, global_vertex);

    std::pair<EntityKey, Vertex>
    global_to_local = std::make_pair(global_vertex, local_vertex);

    local_global_vertex_map_.insert(local_to_global);

    global_local_vertex_map_.insert(global_to_local);

    // store entity rank to vertex property
    VertexNamePropertyMap
    vertex_property_map = boost::get(VertexName(), *this);

    boost::put(vertex_property_map, local_vertex, vertex_rank);
  }

  // Add edges to the subgraph
  for (std::set<stkEdge>::iterator edge_iterator = first_edge;
      edge_iterator != last_edge;
      ++edge_iterator) {

    // Get the edge
    stkEdge
    global_edge = *edge_iterator;

    // Get global source and target vertices
    EntityKey
    global_source_vertex = global_edge.source;

    EntityKey
    global_target_vertex = global_edge.target;

    // Get local source and target vertices
    Vertex
    local_source_vertex =
        global_local_vertex_map_.find(global_source_vertex)->second;

    Vertex
    local_target_vertex =
        global_local_vertex_map_.find(global_target_vertex)->second;

    EdgeId
    edge_id = global_edge.local_id;

    Edge
    local_edge;

    bool
    is_inserted;

    boost::tie(local_edge, is_inserted) =
        boost::add_edge(local_source_vertex, local_target_vertex, *this);

    assert(is_inserted == true);

    // Add edge id to edge property
    EdgeNamePropertyMap
    edge_property_map = boost::get(EdgeName(), *this);

    boost::put(edge_property_map, local_edge, edge_id);
  }

  return;
}

//
// Accessors and mutators
//
Topology &
Subgraph::getTopology()
{return topology_;}

size_t const
Subgraph::getSpaceDimension()
{return getTopology().getSpaceDimension();}

RCP<Albany::AbstractSTKMeshStruct> &
Subgraph::getSTKMeshStruct()
{return getTopology().getSTKMeshStruct();}

stk::mesh::BulkData *
Subgraph::getBulkData()
{return getTopology().getBulkData();}

stk::mesh::MetaData *
Subgraph::getMetaData()
{return getTopology().getMetaData();}

stk::mesh::EntityRank const
Subgraph::getBoundaryRank()
{return getTopology().getBoundaryRank();}

IntScalarFieldType &
Subgraph::getFractureState(stk::mesh::EntityRank rank)
{return getTopology().getFractureState(rank);}

void
Subgraph::setFractureState(Entity e, FractureState const fs)
{getTopology().setFractureState(e, fs);}

FractureState
Subgraph::getFractureState(Entity e)
{return getTopology().getFractureState(e);}

bool
Subgraph::isOpen(Entity e)
{return getTopology().isOpen(e);}

bool
Subgraph::isInternalAndOpen(Entity e)
{return getTopology().isInternalAndOpen(e);}

//
// Map a vertex in the subgraph to a entity in the stk mesh.
//
EntityKey
Subgraph::localToGlobal(Vertex local_vertex)
{
  std::map<Vertex, EntityKey>::const_iterator
  vertex_map_iterator = local_global_vertex_map_.find(local_vertex);

  assert(vertex_map_iterator != local_global_vertex_map_.end());

  return (*vertex_map_iterator).second;
}

//
// Map a entity in the stk mesh to a vertex in the subgraph.
//
Vertex
Subgraph::globalToLocal(EntityKey global_vertex_key)
{
  std::map<EntityKey, Vertex>::const_iterator
  vertex_map_iterator = global_local_vertex_map_.find(global_vertex_key);

  assert(vertex_map_iterator != global_local_vertex_map_.end());

  return (*vertex_map_iterator).second;
}

//
// Add a vertex in the subgraph.
//
Vertex
Subgraph::addVertex(stk::mesh::EntityRank vertex_rank)
{
  // Insert the vertex into the stk mesh
  // First have to request a new entity of rank N
  // number of entity ranks: 1 + number of dimensions
  std::vector<size_t>
  requests(getSpaceDimension() + 1, 0);

  requests[vertex_rank] = 1;

  EntityVector
  new_entities;

  getBulkData()->generate_new_entities(requests, new_entities);

  EntityKey
  global_vertex = getBulkData()->entity_key(new_entities[0]);

  // Add the vertex to the subgraph
  Vertex
  local_vertex = boost::add_vertex(*this);

  // Update maps
  std::pair<Vertex, EntityKey>
  local_to_global = std::make_pair(local_vertex, global_vertex);

  std::pair<EntityKey, Vertex>
  global_to_local = std::make_pair(global_vertex, local_vertex);

  local_global_vertex_map_.insert(local_to_global);

  global_local_vertex_map_.insert(global_to_local);

  // store entity rank to the vertex property
  VertexNamePropertyMap
  vertex_property_map = boost::get(VertexName(), *this);

  boost::put(vertex_property_map, local_vertex, vertex_rank);

  return local_vertex;
}

//
// Remove vertex in subgraph
//
void
Subgraph::removeVertex(Vertex const vertex)
{
  // get the global entity key of vertex
  EntityKey
  key = localToGlobal(vertex);

  // look up entity from key
  Entity
  entity = getBulkData()->get_entity(key);

  // remove the vertex and key from global_local_vertex_map_ and
  // local_global_vertex_map_
  global_local_vertex_map_.erase(key);
  local_global_vertex_map_.erase(vertex);

  // remove vertex from subgraph
  // first have to ensure that there are no edges in or out of the vertex
  boost::clear_vertex(vertex, *this);
  // remove the vertex
  boost::remove_vertex(vertex, *this);

  // remove the entity from stk mesh
  bool const
  deleted = getBulkData()->destroy_entity(entity);
  assert(deleted);

  return;
}

//
// Add edge to local graph.
//
std::pair<Edge, bool>
Subgraph::addEdge(
    EdgeId const edge_id,
    Vertex const local_source_vertex,
    Vertex const local_target_vertex)
{
  // get global entities
  EntityKey
  global_source_key = localToGlobal(local_source_vertex);

  EntityKey
  global_target_key = localToGlobal(local_target_vertex);

  Entity
  global_source_vertex = getBulkData()->get_entity(global_source_key);

  Entity
  global_target_vertex = getBulkData()->get_entity(global_target_key);

  assert(getBulkData()->entity_rank(global_source_vertex) -
         getBulkData()->entity_rank(global_target_vertex) == 1);

  // Add edge to local graph
  std::pair<Edge, bool>
  local_edge = boost::add_edge(local_source_vertex, local_target_vertex, *this);

  if (local_edge.second == false) return local_edge;

  // Add edge to stk mesh
  getBulkData()->declare_relation(
      global_source_vertex,
      global_target_vertex,
      edge_id);

  // Add edge id to edge property
  EdgeNamePropertyMap
  edge_property_map = boost::get(EdgeName(), *this);

  boost::put(edge_property_map, local_edge.first, edge_id);

  return local_edge;
}

//
//
//
void
Subgraph::removeEdge(
    Vertex const & local_source_vertex,
    Vertex const & local_target_vertex)
{
  // Get the local id of the edge in the subgraph
  Edge
  edge;

  bool
  inserted;

  boost::tie(edge, inserted) =
      boost::edge(local_source_vertex, local_target_vertex, *this);

  assert(inserted);

  EdgeId
  edge_id = getEdgeId(edge);

  // remove local edge
  boost::remove_edge(local_source_vertex, local_target_vertex, *this);

  // remove relation from stk mesh
  EntityKey
  global_source_id = localToGlobal(local_source_vertex);

  EntityKey
  global_target_id = localToGlobal(local_target_vertex);

  Entity
  global_source_vertex = getBulkData()->get_entity(global_source_id);

  Entity
  global_target_vertex = getBulkData()->get_entity(global_target_id);

  getBulkData()->destroy_relation(
    global_source_vertex,
    global_target_vertex,
    edge_id);

  return;
}

//
//
//
stk::mesh::EntityRank
Subgraph::getVertexRank(Vertex const vertex)
{
  VertexNamePropertyMap
  vertex_property_map = boost::get(VertexName(), *this);
  return boost::get(vertex_property_map, vertex);
}

//
//
//
EdgeId
Subgraph::getEdgeId(Edge const edge)
{
  EdgeNamePropertyMap
  edge_property_map = boost::get(EdgeName(), *this);
  return boost::get(edge_property_map, edge);
}

// Here we use the connected components algorithm, which requires
// and undirected graph. These types are needed to build that graph
// without the overhead that was used for the subgraph.
// The adjacency list type must use a vector container for the vertices
// so that they can be converted to indices to determine the components.
typedef boost::adjacency_list<VectorS, VectorS, Undirected> UGraph;
typedef boost::graph_traits<UGraph>::vertex_descriptor UVertex;
typedef boost::graph_traits<UGraph>::edge_descriptor UEdge;

namespace {

void
writeGraphviz(std::string const & output_filename, UGraph const & graph) {
  // Open output file
  std::ofstream
  gviz_out;

  gviz_out.open(output_filename.c_str(), std::ios::out);

  if (gviz_out.is_open() == false) {
    std::cout << "Unable to open graphviz output file :";
    std::cout << output_filename << '\n';
    return;
  }

  boost::write_graphviz(gviz_out, graph);

  gviz_out.close();

  return;
}

} // anonymous namespace

//
// Function determines whether the input vertex is an articulation
// point of the subgraph.
//
void
Subgraph::testArticulationPoint(
    Vertex const input_vertex,
    size_t & number_components,
    ComponentMap & component_map)
{
  // Map to and from undirected graph and subgraph
  std::map<UVertex, Vertex>
  u_sub_vertex_map;

  std::map<Vertex, UVertex>
  sub_u_vertex_map;

  UGraph
  graph;

  VertexIterator
  vertex_begin;

  VertexIterator
  vertex_end;

  boost::tie(vertex_begin, vertex_end) = vertices(*this);

  // First add the vertices to the graph
  for (VertexIterator i = vertex_begin; i != vertex_end; ++i) {

    Vertex
    vertex = *i;

    if (vertex == input_vertex) continue;

    UVertex
    uvertex = boost::add_vertex(graph);

    // Add to maps
    std::pair<UVertex, Vertex>
    u_sub = std::make_pair(uvertex, vertex);

    std::pair<Vertex, UVertex>
    sub_u = std::make_pair(vertex, uvertex);

    u_sub_vertex_map.insert(u_sub);
    sub_u_vertex_map.insert(sub_u);
  }

  // Then add the edges
  for (VertexIterator i = vertex_begin; i != vertex_end; ++i) {

    Vertex
    source = *i;

    if (source == input_vertex) continue;

    std::map<Vertex, UVertex>::const_iterator
    source_map_iterator = sub_u_vertex_map.find(source);

    UVertex
    usource = (*source_map_iterator).second;

    OutEdgeIterator
    out_edge_begin;

    OutEdgeIterator
    out_edge_end;

    boost::tie(out_edge_begin, out_edge_end) = boost::out_edges(source, *this);

    for (OutEdgeIterator j = out_edge_begin; j != out_edge_end; ++j) {

      Edge
      edge = *j;

      Vertex
      target = boost::target(edge, *this);

      if (target == input_vertex) continue;

      std::map<Vertex, UVertex>::const_iterator
      target_map_iterator = sub_u_vertex_map.find(target);

      UVertex
      utarget = (*target_map_iterator).second;

      boost::add_edge(usource, utarget, graph);

    }
  }

#if defined(DEBUG_LCM_TOPOLOGY)
  writeGraphviz("undirected.dot", graph);
#endif // DEBUG_LCM_TOPOLOGY

  std::vector<size_t>
  components(boost::num_vertices(graph));

  number_components = boost::connected_components(graph, &components[0]);

  for (std::map<UVertex, Vertex>::iterator i = u_sub_vertex_map.begin();
      i != u_sub_vertex_map.end(); ++i) {

    Vertex
    vertex = (*i).second;

    size_t
    u_vertex = static_cast<size_t>((*i).first);

    std::pair<Vertex, size_t>
    component = std::make_pair(vertex, components[u_vertex]);

    component_map.insert(component);
  }

  return;
}

//
// Clones a boundary entity from the subgraph and separates the in-edges
// of the entity.
//
Vertex
Subgraph::cloneBoundaryEntity(Vertex vertex)
{
  stk::mesh::EntityRank
  vertex_rank = getVertexRank(vertex);

  assert(vertex_rank == getBoundaryRank());

  Vertex
  new_vertex = addVertex(vertex_rank);

  // Copy the out_edges of vertex to new_vertex
  OutEdgeIterator
  out_edge_begin;

  OutEdgeIterator
  out_edge_end;

  boost::tie(out_edge_begin, out_edge_end) = boost::out_edges(vertex, *this);

  for (OutEdgeIterator i = out_edge_begin; i != out_edge_end; ++i) {
    Edge
    edge = *i;

    EdgeId
    edge_id = getEdgeId(edge);

    Vertex
    target = boost::target(edge, *this);

    addEdge(edge_id, new_vertex, target);
  }

  // Copy all out edges not in the subgraph to the new vertex
  cloneOutEdges(vertex, new_vertex);

  // Remove one of the edges from vertex, copy to new_vertex
  // Arbitrarily remove the first edge from original vertex
  InEdgeIterator
  in_edge_begin;

  InEdgeIterator
  in_edge_end;

  boost::tie(in_edge_begin, in_edge_end) = boost::in_edges(vertex, *this);

  Edge
  edge = *(in_edge_begin);

  EdgeId
  edge_id = getEdgeId(edge);

  Vertex
  source = boost::source(edge, *this);

  removeEdge(source, vertex);

  // Add edge to new vertex
  addEdge(edge_id, source, new_vertex);

  return new_vertex;
}

//
// Restore element to node connectivity needed by STK.
//
void
Subgraph::updateElementNodeConnectivity(Entity point, ElementNodeMap & map)
{
  for (ElementNodeMap::iterator i = map.begin(); i != map.end(); ++i) {
    Entity
    element = i->first;

    // Identify relation id and remove
    Entity const *
    relations = getBulkData()->begin_nodes(element);

    stk::mesh::ConnectivityOrdinal const *
    ords = getBulkData()->begin_node_ordinals(element);

    size_t const
    num_relations = getBulkData()->num_nodes(element);

    EdgeId
    edge_id;

    bool
    found = false;

    for (size_t j = 0; j < num_relations; ++j) {
      if (relations[j] == point) {
        edge_id = ords[j];
        found = true;
        break;
      }
    }

    assert(found == true);

    getBulkData()->destroy_relation(element, point, edge_id);

    Entity
    new_point = i->second;
    getBulkData()->declare_relation(element, new_point, edge_id);
  }
  return;
}

//
// Splits an articulation point.
//
std::map<Entity, Entity>
Subgraph::splitArticulationPoint(Vertex vertex)
{
  stk::mesh::EntityRank
  vertex_rank = Subgraph::getVertexRank(vertex);

  size_t
  number_components;

  ComponentMap
  components;

  testArticulationPoint(vertex, number_components, components);

  assert(number_components > 0);

  // The function returns an updated connectivity map.
  // If the vertex rank is not node, then this map will be empty.
  std::map<Entity, Entity>
  new_connectivity;

  if (number_components == 1) return new_connectivity;

  // If more than one component, split vertex in subgraph and stk mesh.
  std::vector<Vertex>
  new_vertices(number_components - 1);

  for (std::vector<Vertex>::size_type i = 0; i < new_vertices.size(); ++i) {
    Vertex
    new_vertex = addVertex(vertex_rank);

    new_vertices[i] = new_vertex;
  }

  // Create a map of elements to new node numbers
  // only if the input vertex is a node
  if (vertex_rank == NODE_RANK) {
    Entity
    point = getBulkData()->get_entity(localToGlobal(vertex));

    for (ComponentMap::iterator i = components.begin();
        i != components.end(); ++i) {

      Vertex
      current_vertex = (*i).first;

      size_t
      component_number = (*i).second;

      stk::mesh::EntityRank
      current_rank = getVertexRank(current_vertex);

      if (current_rank != ELEMENT_RANK) continue;

      if (component_number == number_components - 1) continue;

      Entity
      element = getBulkData()->get_entity(localToGlobal(current_vertex));

      Vertex
      new_vertex = new_vertices[component_number];

      Entity
      new_node = getBulkData()->get_entity(localToGlobal(new_vertex));

      std::pair<Entity, Entity>
      nc = std::make_pair(element, new_node);

      new_connectivity.insert(nc);
    }

    updateElementNodeConnectivity(point, new_connectivity);
  }

  // Copy the out edges of the original vertex to the new vertex
  for (std::vector<Vertex>::size_type i = 0; i < new_vertices.size(); ++i) {
    cloneOutEdges(vertex, new_vertices[i]);
  }

  // Vector for edges to be removed. Vertex is source and edgeId the
  // local id of the edge
  std::vector<std::pair<Vertex, EdgeId> >
  removed;

  // Iterate over the in edges of the vertex to determine which will
  // be removed
  InEdgeIterator
  in_edge_begin;

  InEdgeIterator
  in_edge_end;

  boost::tie(in_edge_begin, in_edge_end) = boost::in_edges(vertex, *this);

  for (InEdgeIterator i = in_edge_begin; i != in_edge_end; ++i) {
    Edge
    edge = *i;

    Vertex
    source = boost::source(edge, *this);

    ComponentMap::const_iterator
    component_iterator = components.find(source);

    size_t
    vertex_component = (*component_iterator).second;

    Entity
    entity = getBulkData()->get_entity(localToGlobal(source));

    if (vertex_component < number_components - 1) {
      EdgeId
      edge_id = getEdgeId(edge);

      removed.push_back(std::make_pair(source, edge_id));
    }
  }

  // Remove all edges in vector removed and replace with new edges
  for (std::vector<std::pair<Vertex, EdgeId> >::iterator i = removed.begin();
      i != removed.end(); ++i) {

    std::pair<Vertex, EdgeId>
    edge = *i;

    Vertex
    source = edge.first;

    EdgeId
    edge_id = edge.second;

    ComponentMap::const_iterator
    component_iterator = components.find(source);

    size_t
    vertex_component = (*component_iterator).second;

    assert(vertex_component < number_components - 1);

    removeEdge(source, vertex);

    Vertex
    new_vertex = new_vertices[vertex_component];

    std::pair<Edge, bool>
    inserted = addEdge(edge_id, source, new_vertex);

    assert(inserted.second == true);
  }

  return new_connectivity;
}

//
// Clone all out edges of a vertex to a new vertex.
//
void
Subgraph::cloneOutEdges(Vertex old_vertex, Vertex new_vertex)
{
  // Get the entity for the old and new vertices
  EntityKey
  old_key = localToGlobal(old_vertex);

  EntityKey
  new_key = localToGlobal(new_vertex);

  Entity
  old_entity = getBulkData()->get_entity(old_key);

  Entity
  new_entity = getBulkData()->get_entity(new_key);

  // Iterate over the out edges of the old vertex and check against the
  // out edges of the new vertex. If the edge does not exist, add.
  assert(getMetaData()->spatial_dimension() == 3);

  stk::mesh::EntityRank const
  one_down = (stk::mesh::EntityRank)(getBulkData()->entity_rank(old_entity) - 1);

  Entity const *
  old_relations = getBulkData()->begin(old_entity, one_down);

  size_t const
  num_old_relations = getBulkData()->num_connectivity(old_entity, one_down);

  stk::mesh::ConnectivityOrdinal const *
  old_relation_ords = getBulkData()->begin_ordinals(old_entity, one_down);

  for (size_t i = 0; i < num_old_relations; ++i) {

    Entity const *
    new_relations = getBulkData()->begin(new_entity, one_down);

    size_t const
    num_new_relations = getBulkData()->num_connectivity(new_entity, one_down);

    // assume the edge doesn't exist
    bool
    exists = false;

    for (size_t j = 0; j < num_new_relations; ++j) {
      if (old_relations[i] == new_relations[j]) {
        exists = true;
        break;
      }
    }

    if (exists == false) {
      EdgeId
      edge_id = old_relation_ords[i];

      Entity
      target = old_relations[i];

      getBulkData()->declare_relation(new_entity, target, edge_id);
    }
  }

  return;
}

//
// \brief Output the graph associated with the mesh to graphviz .dot
// file for visualization purposes.
//
// \param[in] output file
//
// Similar to outputToGraphviz function in Topology class.
// If fracture criterion for entity is satisfied, the entity and all
// associated lower order entities are marked open. All open entities are
// displayed as such in output file.
//
// To create final output figure, run command below from terminal:
//   dot -Tpng <gviz_output>.dot -o <gviz_output>.png
//
void
Subgraph::outputToGraphviz(std::string const & output_filename)
{
  // Open output file
  std::ofstream
  gviz_out;

  gviz_out.open(output_filename.c_str(), std::ios::out);

  if (gviz_out.is_open() == false) {
    std::cout << "Unable to open graphviz output file: ";
    std::cout << output_filename << '\n';
    return;
  }

  std::cout << "Write graph to graphviz dot file: ";
  std::cout << output_filename << '\n';

  // Write beginning of file
  gviz_out << dot_header();

  VertexIterator
  vertices_begin;

  VertexIterator
  vertices_end;

  boost::tie(vertices_begin, vertices_end) = vertices(*this);

  for (VertexIterator i = vertices_begin; i != vertices_end; ++i) {

    EntityKey
    key = localToGlobal(*i);

    Entity
    entity = getBulkData()->get_entity(key);

    stk::mesh::EntityRank const
    rank = getBulkData()->entity_rank(entity);

    FractureState const
    fracture_state = getFractureState(entity);

    EntityId const
    entity_id = getBulkData()->identifier(entity);

    gviz_out << dot_entity(entity_id, rank, fracture_state);

    // write the edges in the subgraph
    OutEdgeIterator
    out_edge_begin;

    OutEdgeIterator
    out_edge_end;

    boost::tie(out_edge_begin, out_edge_end) = boost::out_edges(*i, *this);

    for (OutEdgeIterator j = out_edge_begin; j != out_edge_end; ++j) {

      Edge
      out_edge = *j;

      Vertex
      source = boost::source(out_edge, *this);

      Vertex
      target = boost::target(out_edge, *this);

      EntityKey
      source_key = localToGlobal(source);

      Entity
      global_source = getBulkData()->get_entity(source_key);

      EntityKey
      target_key = localToGlobal(target);

      Entity
      global_target = getBulkData()->get_entity(target_key);

      EdgeId
      edge_id = getEdgeId(out_edge);

      gviz_out << dot_relation(
        getBulkData()->identifier(global_source),
        getBulkData()->entity_rank(global_source),
        getBulkData()->identifier(global_target),
        getBulkData()->entity_rank(global_target),
        edge_id
      );

    }

  }

  // File end
  gviz_out << dot_footer();

  gviz_out.close();

  return;
}

} // namespace LCM
