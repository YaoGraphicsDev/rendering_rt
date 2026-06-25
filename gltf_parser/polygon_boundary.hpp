#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

struct PolygonBoundaryExtractor
{
    struct EdgeKey
    {
        uint16_t a = 0;
        uint16_t b = 0;

        bool operator==(const EdgeKey& other) const
        {
            return a == other.a && b == other.b;
        }
    };

    struct EdgeKeyHash
    {
        std::size_t operator()(const EdgeKey& e) const
        {
            return (std::size_t(e.a) << 16) ^ std::size_t(e.b);
        }
    };

    struct DirectedEdge
    {
        uint16_t from = 0;
        uint16_t to = 0;
    };

    static std::vector<uint16_t> extract(
        const uint8_t* raw_index_data,
        size_t index_count)
    {
        if (index_count % 3 != 0)
        {
            throw std::runtime_error(
                "PolygonBoundaryExtractor: index count is not a triangle list.");
        }

        std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> edge_counts;
        std::vector<DirectedEdge> directed_edges;
        directed_edges.reserve(index_count);

        for (size_t i = 0; i < index_count; i += 3)
        {
            uint16_t i0 = read_index(raw_index_data, i + 0);
            uint16_t i1 = read_index(raw_index_data, i + 1);
            uint16_t i2 = read_index(raw_index_data, i + 2);

            add_edge(edge_counts, directed_edges, i0, i1);
            add_edge(edge_counts, directed_edges, i1, i2);
            add_edge(edge_counts, directed_edges, i2, i0);
        }

        // Boundary adjacency using the original triangle edge direction.
        std::unordered_map<uint16_t, uint16_t> next_vertex;
        std::unordered_map<uint16_t, uint32_t> incoming_count;
        std::unordered_map<uint16_t, uint32_t> outgoing_count;

        for (const DirectedEdge& edge : directed_edges)
        {
            EdgeKey key = make_edge(edge.from, edge.to);

            if (edge_counts[key] == 1)
            {
                if (next_vertex.find(edge.from) != next_vertex.end())
                {
                    throw std::runtime_error(
                        "PolygonBoundaryExtractor: boundary is not a single simple loop.");
                }

                next_vertex[edge.from] = edge.to;
                outgoing_count[edge.from]++;
                incoming_count[edge.to]++;
            }
        }

        if (next_vertex.empty())
        {
            throw std::runtime_error(
                "PolygonBoundaryExtractor: no boundary found.");
        }

        for (const auto& pair : next_vertex)
        {
            uint16_t v = pair.first;

            if (outgoing_count[v] != 1 || incoming_count[v] != 1)
            {
                throw std::runtime_error(
                    "PolygonBoundaryExtractor: boundary is not a single simple loop.");
            }
        }

        return walk_boundary(next_vertex);
    }

    static std::vector<glm::vec3> remove_collinear_vertices(
        const std::vector<glm::vec3>& polygon,
        float eps = 1e-6f)
    {
        if (polygon.size() <= 2)
            return polygon;

        std::vector<glm::vec3> result;
        const size_t n = polygon.size();

        for (size_t i = 0; i < n; ++i)
        {
            const glm::vec3& prev = polygon[(i + n - 1) % n];
            const glm::vec3& curr = polygon[i];
            const glm::vec3& next = polygon[(i + 1) % n];

            glm::vec3 e0 = curr - prev;
            glm::vec3 e1 = next - curr;

            float len0Sq = glm::dot(e0, e0);
            float len1Sq = glm::dot(e1, e1);

            bool removable = false;

            // Remove duplicate / degenerate points
            if (len0Sq < eps * eps || len1Sq < eps * eps)
            {
                removable = true;
            }
            else
            {
                glm::vec3 cross = glm::cross(e0, e1);

                // Scale-aware collinearity test
                removable =
                    glm::dot(cross, cross) <= eps * eps * len0Sq * len1Sq;
            }

            if (!removable)
                result.push_back(curr);
        }

        return result;
    }

    /*
    * a -- d
    * |    |
    * b -- c
    */
    static bool is_planar_rectangle(const std::vector<glm::vec3> boundary, float eps = 1e-6f) {
        if (boundary.size() != 4) {
            return false;
        }

        glm::vec3 a = boundary[0];
        glm::vec3 b = boundary[1];
        glm::vec3 c = boundary[2];
        glm::vec3 d = boundary[3];
        glm::vec3 ab = b - a;
        glm::vec3 bc = c - b;
        glm::vec3 cd = d - c;
        glm::vec3 da = a - d;

        float ab2 = glm::dot(ab, ab);
        float bc2 = glm::dot(bc, bc);
        float cd2 = glm::dot(cd, cd);
        float da2 = glm::dot(da, da);

        // Degenerate edge check
        if (ab2 < eps * eps || bc2 < eps * eps ||
            cd2 < eps * eps || da2 < eps * eps) {
            return false;
        }

        // Coplanar check
        glm::vec3 n = glm::cross(ab, bc);
        float l = glm::length(n);

        if (l < eps) {
            return false;
        }

        float distance = std::abs(glm::dot(n, d - a)) / l;

        if (distance > eps) {
            return false;
        }

        // Adjacent edges should be perpendicular
        if (std::abs(glm::dot(ab, bc)) > eps * std::sqrt(ab2 * bc2)) {
            return false;
        }

        if (std::abs(glm::dot(bc, cd)) > eps * std::sqrt(bc2 * cd2)) {
            return false;
        }

        // Opposite edges should have equal length
        if (std::abs(ab2 - cd2) > eps * std::max(ab2, cd2)) {
            return false;
        }

        if (std::abs(bc2 - da2) > eps * std::max(bc2, da2)) {
            return false;
        }

        return true;
    }


private:
    static uint16_t read_index(
        const uint8_t* raw_index_data,
        size_t index_index)
    {
        uint16_t value = 0;

        std::memcpy(
            &value,
            raw_index_data + index_index * sizeof(uint16_t),
            sizeof(uint16_t));

        return value;
    }

    static EdgeKey make_edge(uint16_t a, uint16_t b)
    {
        if (a < b)
            return { a, b };

        return { b, a };
    }

    static void add_edge(
        std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash>& edge_counts,
        std::vector<DirectedEdge>& directed_edges,
        uint16_t from,
        uint16_t to)
    {
        EdgeKey edge = make_edge(from, to);

        edge_counts[edge]++;
        directed_edges.push_back({ from, to });
    }

    static std::vector<uint16_t> walk_boundary(
        const std::unordered_map<uint16_t, uint16_t>& next_vertex)
    {
        std::vector<uint16_t> result;
        result.reserve(next_vertex.size());

        uint16_t start = next_vertex.begin()->first;
        uint16_t current = start;

        while (true)
        {
            result.push_back(current);

            auto it = next_vertex.find(current);
            if (it == next_vertex.end())
            {
                throw std::runtime_error(
                    "PolygonBoundaryExtractor: boundary traversal failed.");
            }

            current = it->second;

            if (current == start)
                break;

            if (result.size() > next_vertex.size())
            {
                throw std::runtime_error(
                    "PolygonBoundaryExtractor: boundary traversal failed.");
            }
        }

        if (result.size() != next_vertex.size())
        {
            throw std::runtime_error(
                "PolygonBoundaryExtractor: boundary is not a single simple loop.");
        }

        return result;
    }
};