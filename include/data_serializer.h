/**
 * DataSerializer - Abstract base class for format-agnostic DIC data I/O
 * 
 * The MATLAB data structure (mat_trees.txt) is the canonical schema.
 * This abstraction allows transparent switching between output formats
 * (.mat, .bin, .json) via a config parameter, without changing pipeline code.
 * 
 * To add a new format, implement a new subclass and register it in create().
 */

#ifndef DATA_SERIALIZER_H
#define DATA_SERIALIZER_H

#include "dic_structures.h"
#include <string>
#include <memory>

namespace cppxdic {

class DataSerializer {
public:
    virtual ~DataSerializer() = default;

    /** File extension for this format (e.g., ".mat", ".bin", ".json") */
    virtual std::string extension() const = 0;

    // =========================================================================
    // DIC3Dcombined (Step E output / Step F input)
    // =========================================================================
    virtual bool saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) = 0;
    virtual bool loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) = 0;

    // =========================================================================
    // DIC3DPPresults (Step F output)
    // =========================================================================
    virtual bool saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) = 0;

    // =========================================================================
    // DIC2DPairResults (Step D output / Step E helper)
    // =========================================================================
    virtual bool saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) = 0;
    virtual bool loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) = 0;

    // =========================================================================
    // Utility: check if a file with this serializer's extension exists
    // =========================================================================
    bool fileExists(const std::string& basePath) const;

    // =========================================================================
    // Factory
    // =========================================================================
    /**
     * Create a serializer for the given format string.
     * @param format "mat", "bin", or "json"
     * @return Concrete serializer instance
     * @throws std::invalid_argument if format is unknown
     */
    static std::unique_ptr<DataSerializer> create(const std::string& format);
};

// ============================================================================
// MatSerializer — wraps MatWriter / MatReader
// ============================================================================
class MatSerializer : public DataSerializer {
public:
    std::string extension() const override { return ".mat"; }
    bool saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) override;
    bool loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) override;
    bool saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) override;
    bool saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) override;
    bool loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) override;
};

// ============================================================================
// BinarySerializer — wraps saveBinary / loadBinary
// ============================================================================
class BinarySerializer : public DataSerializer {
public:
    std::string extension() const override { return ".bin"; }
    bool saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) override;
    bool loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) override;
    bool saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) override;
    bool saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) override;
    bool loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) override;
};

// ============================================================================
// JsonSerializer — uses nlohmann/json following MATLAB structure
// ============================================================================
class JsonSerializer : public DataSerializer {
public:
    std::string extension() const override { return ".json"; }
    bool saveDIC3Dcombined(const std::string& path, const DIC3Dcombined& data) override;
    bool loadDIC3Dcombined(const std::string& path, DIC3Dcombined& data) override;
    bool saveDIC3DPPresults(const std::string& path, const DIC3DPPresults& data) override;
    bool saveDIC2DPairResults(const std::string& path, const DIC2DPairResults& data) override;
    bool loadDIC2DPairResults(const std::string& path, DIC2DPairResults& data) override;
};

} // namespace cppxdic

#endif // DATA_SERIALIZER_H
