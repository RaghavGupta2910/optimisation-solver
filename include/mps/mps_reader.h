#ifndef MPS_MPS_READER_H_
#define MPS_MPS_READER_H_

#include <string>
#include <sstream>
#include <unordered_map>
#include <limits>
#include "model/model.h"

namespace mps {

enum class MpsSection {
    NONE,
    NAME,
    ROWS,
    COLUMNS,
    RHS,
    BOUNDS,
    ENDATA
};

class MpsReader {
public:
    MpsReader() = default;

    // Parses a basic LP MPS file and returns a validated model::Model object
    model::Model read(const std::string& filepath);

private:
    MpsSection current_section_ = MpsSection::NONE;
    std::string objective_row_name_;
    
    // Fast index mappings for populating model::Model
    std::unordered_map<std::string, size_t> var_name_to_idx_;
    std::unordered_map<std::string, size_t> constraint_name_to_idx_;

    // Helpers
    size_t get_or_create_variable(const std::string& var_name, model::Model& model);
    void add_coefficient(const std::string& var_name, const std::string& row_name, double value, model::Model& model);
};

} // namespace mps

#endif // MPS_MPS_READER_H_