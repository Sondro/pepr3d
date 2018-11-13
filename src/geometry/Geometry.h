#pragma once

#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/exceptions.h>

#include <cassert>
#include <optional>
#include <vector>

#include "cinder/Log.h"
#include "cinder/Ray.h"
#include "cinder/gl/gl.h"
#include "geometry/ColorManager.h"
#include "geometry/ModelImporter.h"
#include "geometry/PolyhedronBuilder.h"
#include "geometry/Triangle.h"

namespace pepr3d {

using Direction = K::Direction_3;
using Ft = K::FT;
using Ray = K::Ray_3;
using My_AABB_traits = CGAL::AABB_traits<K, DataTriangleAABBPrimitive>;
using Tree = CGAL::AABB_tree<My_AABB_traits>;
using Ray_intersection = boost::optional<Tree::Intersection_and_primitive_id<Ray>::Type>;

class Geometry {
   public:
    using ColorIndex = GLuint;

   private:
    /// Triangle soup of the model mesh, containing CGAL::Triangle_3 data for AABB tree.
    std::vector<DataTriangle> mTriangles;

    /// Vertex buffer with the same data as mTriangles for OpenGL to render the mesh.
    /// Contains position and color data for each vertex.
    std::vector<glm::vec3> mVertexBuffer;

    /// Color buffer, keeping the invariant that every triangle has only one color - all three vertices have to have the
    /// same color. It is aligned with the vertex buffer and its size should be always equal to the vertex buffer.
    std::vector<ColorIndex> mColorBuffer;

    /// Normal buffer, the triangle has same normal for its every vertex.
    /// It is aligned with the vertex buffer and its size should be always equal to the vertex buffer.
    std::vector<glm::vec3> mNormalBuffer;

    /// Index buffer for OpenGL frontend., specifying the same triangles as in mTriangles.
    std::vector<uint32_t> mIndexBuffer;

    /// AABB tree from the CGAL library, to find intersections with rays generated by user mouse clicks and the mesh.
    std::unique_ptr<Tree> mTree;

    /// A vector based map mapping size_t into ci::ColorA
    ColorManager mColorManager;

    struct GeometryState {
        std::vector<DataTriangle> triangles;
        ColorManager::ColorMap colorMap;
    };

    struct PolyhedronData {
        std::vector<glm::vec3> vertices;

        std::vector<std::array<size_t, 3>> indices;

        Polyhedron P;

        std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle> faceHandles;

        bool closeCheck = false;
    } mPolyhedronData;

   public:
    /// Empty constructor
    Geometry() {
        mTree = std::make_unique<Tree>();
    }

    Geometry(std::vector<DataTriangle>&& triangles) : mTriangles(std::move(triangles)) {
        generateVertexBuffer();
        generateIndexBuffer();
        generateColorBuffer();
        generateNormalBuffer();
        assert(mIndexBuffer.size() == mVertexBuffer.size());
        mTree = std::make_unique<Tree>(mTriangles.begin(), mTriangles.end());
        assert(mTree->size() == mTriangles.size());
    }

    /// Returns a constant iterator to the vertex buffer
    std::vector<glm::vec3>& getVertexBuffer() {
        return mVertexBuffer;
    }

    bool polyClosedCheck() const {
        return mPolyhedronData.closeCheck;
    }

    size_t polyVertCount() const {
        return mPolyhedronData.vertices.size();
    }

    /// Returns a constant iterator to the index buffer
    std::vector<uint32_t>& getIndexBuffer() {
        return mIndexBuffer;
    }

    std::vector<ColorIndex>& getColorBuffer() {
        return mColorBuffer;
    }

    std::vector<glm::vec3>& getNormalBuffer() {
        return mNormalBuffer;
    }

    /// Loads new geometry into the private data, rebuilds the vertex and index buffers
    /// automatically.
    void loadNewGeometry(const std::string& fileName) {
        /// Load into mTriangles
        ModelImporter modelImporter(fileName);  // only first mesh [0]

        if(modelImporter.isModelLoaded()) {
            mTriangles = modelImporter.getTriangles();

            mPolyhedronData.vertices.clear();
            mPolyhedronData.indices.clear();
            mPolyhedronData.vertices = modelImporter.getVertexBuffer();
            mPolyhedronData.indices = modelImporter.getIndexBuffer();

            /// Generate new vertex buffer
            generateVertexBuffer();

            /// Generate new index buffer
            generateIndexBuffer();

            /// Generate new color buffer from triangle color data
            generateColorBuffer();

            /// Generate new normal buffer, copying the triangle normal to each vertex
            generateNormalBuffer();

            /// Rebuild the AABB tree
            mTree->rebuild(mTriangles.begin(), mTriangles.end());
            assert(mTree->size() == mTriangles.size());

            /// Build the polyhedron data structure
            buildPolyhedron();

            /// Get the generated color palette of the model, replace the current one
            mColorManager = modelImporter.getColorManager();
            assert(!mColorManager.empty());
        } else {
            CI_LOG_E("Model not loaded --> write out message for user");
        }
    }

    /// Set new triangle color. Fast, as it directly modifies the color buffer, without requiring a reload.
    void setTriangleColor(const size_t triangleIndex, const size_t newColor) {
        /// Change it in the buffer
        // Color buffer has 1 ColorA for each vertex, each triangle has 3 vertices
        const size_t vertexPosition = triangleIndex * 3;

        // Change all vertices of the triangle to the same new color
        assert(vertexPosition + 2 < mColorBuffer.size());

        ColorIndex newColorIndex = static_cast<ColorIndex>(newColor);
        mColorBuffer[vertexPosition] = newColorIndex;
        mColorBuffer[vertexPosition + 1] = newColorIndex;
        mColorBuffer[vertexPosition + 2] = newColorIndex;

        /// Change it in the triangle soup
        assert(triangleIndex < mTriangles.size());
        mTriangles[triangleIndex].setColor(newColor);
    }

    /// Get the color of the indexed triangle
    size_t getTriangleColor(const size_t triangleIndex) const {
        assert(triangleIndex < mTriangles.size());
        return mTriangles[triangleIndex].getColor();
    }

    /// Intersects the mesh with the given ray and returns the index of the triangle intersected, if it exists.
    /// Example use: generate ray based on a mouse click, call this method, then call setTriangleColor.
    std::optional<size_t> intersectMesh(const ci::Ray& ray) const {
        if(mTree->empty()) {
            return {};
        }

        const glm::vec3 source = ray.getOrigin();
        const glm::vec3 direction = ray.getDirection();

        const Ray rayQuery(Point(source.x, source.y, source.z), Direction(direction.x, direction.y, direction.z));

        // Find the two intersection parameters - place and triangle
        Ray_intersection intersection = mTree->first_intersection(rayQuery);
        if(intersection) {
            // The intersected triangle
            if(boost::get<DataTriangleAABBPrimitive::Id>(intersection->second) != mTriangles.end()) {
                const DataTriangleAABBPrimitive::Id intersectedTriIter =
                    boost::get<DataTriangleAABBPrimitive::Id>(intersection->second);
                assert(intersectedTriIter != mTriangles.end());
                const size_t retValue = intersectedTriIter - mTriangles.begin();
                assert(retValue < mTriangles.size());
                return retValue;  // convert the iterator into an index
            }
        }

        /// No intersection detected.
        return {};
    }

    /// Return the number of triangles in the model
    size_t getTriangleCount() const {
        return mTriangles.size();
    }

    const ColorManager& getColorManager() const {
        return mColorManager;
    }

    ColorManager& getColorManager() {
        return mColorManager;
    }

    const DataTriangle& getTriangle(const size_t triangleIndex) const {
        assert(triangleIndex >= 0);
        assert(triangleIndex < mTriangles.size());
        return mTriangles[triangleIndex];
    }

    /// Save current state into a struct so that it can be restored later (CommandManager target requirement)
    GeometryState saveState() const;

    /// Load previous state from a struct (CommandManager target requirement)
    void loadState(const GeometryState&);

    /// Spreads color starting from startTriangle to wherever it can reach.
    template <typename StoppingCondition>
    void bucket(const std::size_t startTriangle, const StoppingCondition& stopFunctor) {
        if(mPolyhedronData.P.is_empty()) {
            return;
        }

        std::deque<size_t> toVisit;
        const size_t startingFace = startTriangle;
        toVisit.push_back(startingFace);

        std::unordered_set<size_t> alreadyVisited;
        alreadyVisited.insert(startingFace);

        assert(mPolyhedronData.indices.size() == mTriangles.size());

        while(!toVisit.empty()) {
            // Remove yourself from queue and mark visited
            const size_t currentVertex = toVisit.front();
            toVisit.pop_front();
            assert(alreadyVisited.find(currentVertex) != alreadyVisited.end());
            assert(currentVertex < mTriangles.size());
            assert(toVisit.size() < mTriangles.size());

            // Manage neighbours and grow the queue
            addNeighboursToQueue(currentVertex, mPolyhedronData.faceHandles, alreadyVisited, toVisit, stopFunctor);

            // Set the color
            setTriangleColor(currentVertex, mColorManager.getActiveColorIndex());
        }
    }

   private:
    /// Generates the vertex buffer linearly - adding each vertex of each triangle as a new one.
    /// We need to do this because each triangle has to be able to be colored differently, therefore no vertex sharing
    /// is possible.
    void generateVertexBuffer() {
        mVertexBuffer.clear();
        mVertexBuffer.reserve(3 * mTriangles.size());

        for(const auto& mTriangle : mTriangles) {
            mVertexBuffer.push_back(mTriangle.getVertex(0));
            mVertexBuffer.push_back(mTriangle.getVertex(1));
            mVertexBuffer.push_back(mTriangle.getVertex(2));
        }
    }

    /// Generating a linear index buffer, since we do not reuse any vertices.
    void generateIndexBuffer() {
        mIndexBuffer.clear();
        mIndexBuffer.reserve(mVertexBuffer.size());

        for(uint32_t i = 0; i < mVertexBuffer.size(); ++i) {
            mIndexBuffer.push_back(i);
        }
    }

    /// Generating triplets of colors, since we only allow a single-colored triangle.
    void generateColorBuffer() {
        mColorBuffer.clear();
        mColorBuffer.reserve(mVertexBuffer.size());

        for(const auto& mTriangle : mTriangles) {
            const ColorIndex triColorIndex = static_cast<ColorIndex>(mTriangle.getColor());
            mColorBuffer.push_back(triColorIndex);
            mColorBuffer.push_back(triColorIndex);
            mColorBuffer.push_back(triColorIndex);
        }
        assert(mColorBuffer.size() == mVertexBuffer.size());
    }

    /// Generate a buffer of normals. Generates only "triangle normals" - all three vertices have the same normal.
    void generateNormalBuffer() {
        mNormalBuffer.clear();
        mNormalBuffer.reserve(mVertexBuffer.size());
        for(const auto& mTriangle : mTriangles) {
            mNormalBuffer.push_back(mTriangle.getNormal());
            mNormalBuffer.push_back(mTriangle.getNormal());
            mNormalBuffer.push_back(mTriangle.getNormal());
        }
        assert(mNormalBuffer.size() == mVertexBuffer.size());
    }

    /// Build the CGAL Polyhedron construct in mPolyhedronData. Takes a bit of time to rebuild.
    void buildPolyhedron() {
        PolyhedronBuilder<HalfedgeDS> triangle(mPolyhedronData.indices, mPolyhedronData.vertices);
        mPolyhedronData.P.clear();
        try {
            mPolyhedronData.P.delegate(triangle);
            mPolyhedronData.faceHandles = triangle.getFacetArray();
        } catch(CGAL::Assertion_exception assertExcept) {
            mPolyhedronData.P.clear();
            CI_LOG_E("Polyhedron not loaded. " + assertExcept.message());
            return;
        }

        assert(mPolyhedronData.P.size_of_facets() == mPolyhedronData.indices.size());
        assert(mPolyhedronData.P.size_of_vertices() == mPolyhedronData.vertices.size());

        // Use the facetsCreated from the incremental builder, set the ids linearly
        for(int facetId = 0; facetId < mPolyhedronData.faceHandles.size(); ++facetId) {
            mPolyhedronData.faceHandles[facetId]->id() = facetId;
        }

        mPolyhedronData.closeCheck = mPolyhedronData.P.is_closed();
    }

    /// Used by BFS in bucket painting. Aggregates the neighbours of the triangle at triIndex by looking into the CGAL
    /// Polyhedron construct.
    std::array<int, 3> gatherNeighbours(
        const size_t triIndex,
        const std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle>& faceHandles) const {
        assert(triIndex < faceHandles.size());
        const Polyhedron::Facet_iterator& facet = faceHandles[triIndex];
        std::array<int, 3> returnValue = {-1, -1, -1};
        assert(facet->is_triangle());

        const auto edgeIteratorStart = facet->facet_begin();
        auto edgeIter = edgeIteratorStart;

        for(int i = 0; i < 3; ++i) {
            const auto eFace = edgeIter->facet();
            if(edgeIter->opposite()->facet() != nullptr) {
                const size_t triId = edgeIter->opposite()->facet()->id();
                assert(static_cast<int>(triId) < mTriangles.size());
                returnValue[i] = static_cast<int>(triId);
            }
            ++edgeIter;
        }
        assert(edgeIter == edgeIteratorStart);

        return returnValue;
    }

    /// Used by BFS in bucket painting. Manages the queue used to search through the graph.
    template <typename StoppingCondition>
    void addNeighboursToQueue(
        const size_t currentVertex,
        const std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle>& faceHandles,
        std::unordered_set<size_t>& alreadyVisited, std::deque<size_t>& toVisit,
        const StoppingCondition& stopFunctor) const {
        const std::array<int, 3> neighbours = gatherNeighbours(currentVertex, faceHandles);
        for(int i = 0; i < 3; ++i) {
            if(neighbours[i] == -1) {
                continue;
            } else {
                if(alreadyVisited.find(neighbours[i]) == alreadyVisited.end()) {
                    // New vertex -> visit it.
                    if(stopFunctor(neighbours[i], currentVertex)) {
                        toVisit.push_back(neighbours[i]);
                        alreadyVisited.insert(neighbours[i]);
                    }
                }
            }
        }
    }
};

}  // namespace pepr3d