/**
 * Delaunay Triangulation Implementation using CGAL
 */

#include "delaunay_triangulation.h"
#include "logging.h"
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <iostream>
#include <map>
#include <cmath>

namespace cppxdic {

// CGAL type definitions
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Triangulation_vertex_base_with_info_2<size_t, K> Vb;
typedef CGAL::Triangulation_data_structure_2<Vb> Tds;
typedef CGAL::Delaunay_triangulation_2<K, Tds> Delaunay;
typedef K::Point_2 Point_2;

std::vector<int> DelaunayTriangulation::compute(const std::vector<cv::Point2f>& points) {
    std::vector<int> faces;
    
    if (points.size() < 3) {
        LOG_ERROR << "Need at least 3 points for triangulation";
        return faces;
    }
    
    // Create Delaunay triangulation with vertex info (for indexing)
    Delaunay dt;
    
    // Insert points with their original indices
    std::vector<std::pair<Point_2, size_t>> cgal_points;
    for (size_t i = 0; i < points.size(); ++i) {
        cgal_points.push_back(std::make_pair(
            Point_2(points[i].x, points[i].y), i
        ));
    }
    
    dt.insert(cgal_points.begin(), cgal_points.end());
    
    // Extract triangles (faces)
    for (auto fit = dt.finite_faces_begin(); fit != dt.finite_faces_end(); ++fit) {
        // Get vertex indices (stored as info)
        size_t v0 = fit->vertex(0)->info();
        size_t v1 = fit->vertex(1)->info();
        size_t v2 = fit->vertex(2)->info();
        
        // MATLAB uses 1-based indexing, but we'll use 0-based in C++
        // and convert when writing to .mat files
        faces.push_back(v0);
        faces.push_back(v1);
        faces.push_back(v2);
    }
    
    LOG_INFO << "Delaunay triangulation: " << points.size() << " points -> "
             << (faces.size() / 3) << " triangles";
    
    return faces;
}

std::vector<int> DelaunayTriangulation::filterByEdgeLength(
    const std::vector<int>& faces,
    const std::vector<cv::Point2f>& vertices,
    double max_edge_threshold) {
    
    std::vector<int> filtered_faces;
    
    if (faces.size() % 3 != 0) {
        LOG_ERROR << "Faces array size must be multiple of 3";
        return filtered_faces;
    }
    
    size_t n_triangles = faces.size() / 3;
    
    for (size_t i = 0; i < n_triangles; ++i) {
        size_t idx = i * 3;
        int v0 = faces[idx];
        int v1 = faces[idx + 1];
        int v2 = faces[idx + 2];
        
        // Check bounds (explicit negative check before implicit unsigned conversion)
        if (v0 < 0 || v1 < 0 || v2 < 0 ||
            static_cast<size_t>(v0) >= vertices.size() || static_cast<size_t>(v1) >= vertices.size() || static_cast<size_t>(v2) >= vertices.size()) {
            if (i < 5) {
                LOG_WARN << "Invalid vertex index in triangle " << i
                         << " (v0=" << v0 << " v1=" << v1 << " v2=" << v2
                         << ", nVerts=" << vertices.size() << ")";
            }
            continue;
        }
        
        // Compute edge lengths
        cv::Point2f p0 = vertices[v0];
        cv::Point2f p1 = vertices[v1];
        cv::Point2f p2 = vertices[v2];
        
        double e01 = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
        double e12 = std::sqrt((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));
        double e20 = std::sqrt((p0.x - p2.x) * (p0.x - p2.x) + (p0.y - p2.y) * (p0.y - p2.y));
        
        double max_edge = std::max({e01, e12, e20});
        
        // Keep triangle if max edge is below threshold
        if (max_edge <= max_edge_threshold) {
            filtered_faces.push_back(v0);
            filtered_faces.push_back(v1);
            filtered_faces.push_back(v2);
        }
    }
    
    size_t removed = n_triangles - (filtered_faces.size() / 3);
    if (removed > 0) {
        LOG_INFO << "Filtered " << removed << " irregular triangles (edge > "
                 << max_edge_threshold << ")";
    }
    
    return filtered_faces;
}

std::vector<double> DelaunayTriangulation::computeEdgeLengths(
    const std::vector<int>& faces,
    const std::vector<cv::Point2f>& vertices) {
    
    std::vector<double> edge_lengths;
    
    if (faces.size() % 3 != 0) {
        LOG_ERROR << "Faces array size must be multiple of 3";
        return edge_lengths;
    }
    
    size_t n_triangles = faces.size() / 3;
    edge_lengths.reserve(n_triangles * 3);
    
    for (size_t i = 0; i < n_triangles; ++i) {
        size_t idx = i * 3;
        int v0 = faces[idx];
        int v1 = faces[idx + 1];
        int v2 = faces[idx + 2];
        
        if (v0 >= vertices.size() || v1 >= vertices.size() || v2 >= vertices.size()) {
            // Invalid triangle, add NaN
            edge_lengths.push_back(std::nan(""));
            edge_lengths.push_back(std::nan(""));
            edge_lengths.push_back(std::nan(""));
            continue;
        }
        
        cv::Point2f p0 = vertices[v0];
        cv::Point2f p1 = vertices[v1];
        cv::Point2f p2 = vertices[v2];
        
        double e01 = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
        double e12 = std::sqrt((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));
        double e20 = std::sqrt((p0.x - p2.x) * (p0.x - p2.x) + (p0.y - p2.y) * (p0.y - p2.y));
        
        edge_lengths.push_back(e01);
        edge_lengths.push_back(e12);
        edge_lengths.push_back(e20);
    }
    
    return edge_lengths;
}

std::vector<int> DelaunayTriangulation::flipOrientation(const std::vector<int>& faces) {
    std::vector<int> flipped;
    
    if (faces.size() % 3 != 0) {
        LOG_ERROR << "Faces array size must be multiple of 3";
        return flipped;
    }
    
    flipped.reserve(faces.size());
    
    // MATLAB: F(:, [1 3 2]) - swap 2nd and 3rd vertices
    for (size_t i = 0; i < faces.size(); i += 3) {
        flipped.push_back(faces[i]);      // v0
        flipped.push_back(faces[i + 2]);  // v2 (swapped)
        flipped.push_back(faces[i + 1]);  // v1 (swapped)
    }
    
    return flipped;
}

std::vector<cv::Point2f> DelaunayTriangulation::computeCentroids(
    const std::vector<int>& faces,
    const std::vector<cv::Point2f>& vertices) {
    
    std::vector<cv::Point2f> centroids;
    
    if (faces.size() % 3 != 0) {
        LOG_ERROR << "Faces array size must be multiple of 3";
        return centroids;
    }
    
    size_t n_triangles = faces.size() / 3;
    centroids.reserve(n_triangles);
    
    for (size_t i = 0; i < n_triangles; ++i) {
        size_t idx = i * 3;
        int v0 = faces[idx];
        int v1 = faces[idx + 1];
        int v2 = faces[idx + 2];
        
        if (v0 >= vertices.size() || v1 >= vertices.size() || v2 >= vertices.size()) {
            // Invalid triangle
            centroids.push_back(cv::Point2f(std::nan(""), std::nan("")));
            continue;
        }
        
        cv::Point2f p0 = vertices[v0];
        cv::Point2f p1 = vertices[v1];
        cv::Point2f p2 = vertices[v2];
        
        float cx = (p0.x + p1.x + p2.x) / 3.0f;
        float cy = (p0.y + p1.y + p2.y) / 3.0f;
        
        centroids.push_back(cv::Point2f(cx, cy));
    }
    
    return centroids;
}

std::vector<double> DelaunayTriangulation::computeAreas(
    const std::vector<int>& faces,
    const std::vector<cv::Point2f>& vertices) {
    
    std::vector<double> areas;
    
    if (faces.size() % 3 != 0) {
        LOG_ERROR << "Faces array size must be multiple of 3";
        return areas;
    }
    
    size_t n_triangles = faces.size() / 3;
    areas.reserve(n_triangles);
    
    for (size_t i = 0; i < n_triangles; ++i) {
        size_t idx = i * 3;
        int v0 = faces[idx];
        int v1 = faces[idx + 1];
        int v2 = faces[idx + 2];
        
        if (v0 >= vertices.size() || v1 >= vertices.size() || v2 >= vertices.size()) {
            // Invalid triangle
            areas.push_back(std::nan(""));
            continue;
        }
        
        cv::Point2f p0 = vertices[v0];
        cv::Point2f p1 = vertices[v1];
        cv::Point2f p2 = vertices[v2];
        
        // Triangle area using cross product: |AB × AC| / 2
        double area = 0.5 * std::abs(
            (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y)
        );
        
        areas.push_back(area);
    }
    
    return areas;
}

} // namespace cppxdic
