#include "mps/mps_reader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

namespace mps {

model::Model MpsReader::read(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("MpsReader Error: Unable to open file " + filepath);
    }

    model::Model model;
    std::string line;
    current_section_ = MpsSection::NONE;
    objective_row_name_ = "";
    var_name_to_idx_.clear();
    constraint_name_to_idx_.clear();

    while (std::getline(file, line)) {
        // Skip empty lines and comment lines
        if (line.empty() || line[0] == '*') continue;

        std::stringstream ss(line);
        std::string token;
        ss >> token;

        // Check for Header Section indicators (starts in column 1)
        if (line[0] != ' ' && line[0] != '\t') {
            if (token == "NAME") {
                current_section_ = MpsSection::NAME;
                if (ss >> token) {
                    model.name = token;
                }
            } else if (token == "ROWS") {
                current_section_ = MpsSection::ROWS;
            } else if (token == "COLUMNS") {
                current_section_ = MpsSection::COLUMNS;
            } else if (token == "RHS") {
                current_section_ = MpsSection::RHS;
            } else if (token == "BOUNDS") {
                current_section_ = MpsSection::BOUNDS;
            } else if (token == "ENDATA") {
                current_section_ = MpsSection::ENDATA;
                break;
            }
            continue;
        }

        // Process Data Lines inside Sections
        switch (current_section_) {
            case MpsSection::ROWS: {
                std::string sense = token;
                std::string row_name;
                ss >> row_name;

                if (sense == "N") {
                    if (objective_row_name_.empty()) {
                        objective_row_name_ = row_name;
                        model.objective.name = row_name;
                    }
                } else {
                    model::Constraint constraint;
                    constraint.name = row_name;
                    
                    // Unified bound representation
                    if (sense == "L") {
                        constraint.lowerBound = -std::numeric_limits<double>::infinity();
                        constraint.upperBound = 0.0;
                    } else if (sense == "G") {
                        constraint.lowerBound = 0.0;
                        constraint.upperBound = std::numeric_limits<double>::infinity();
                    } else if (sense == "E") {
                        constraint.lowerBound = 0.0;
                        constraint.upperBound = 0.0;
                    }

                    size_t idx = model.constraints.size();
                    model.constraints.push_back(constraint);
                    constraint_name_to_idx_[row_name] = idx;
                }
                break;
            }

            case MpsSection::COLUMNS: {
                std::string var_name = token;
                std::string row_name1;
                double val1;

                if (ss >> row_name1 >> val1) {
                    add_coefficient(var_name, row_name1, val1, model);
                }

                // Optional 2nd tuple on the same line
                std::string row_name2;
                double val2;
                if (ss >> row_name2 >> val2) {
                    add_coefficient(var_name, row_name2, val2, model);
                }
                break;
            }

            case MpsSection::RHS: {
                std::string rhs_label = token;
                std::string row_name;
                double val;

                while (ss >> row_name >> val) {
                    auto it = constraint_name_to_idx_.find(row_name);
                    if (it != constraint_name_to_idx_.end()) {
                        auto& c = model.constraints[it->second];
                        // Update bounds based on RHS value
                        if (c.upperBound == 0.0 && c.lowerBound == -std::numeric_limits<double>::infinity()) {
                            c.upperBound = val; // <= b
                        } else if (c.lowerBound == 0.0 && c.upperBound == std::numeric_limits<double>::infinity()) {
                            c.lowerBound = val; // >= b
                        } else if (c.lowerBound == 0.0 && c.upperBound == 0.0) {
                            c.lowerBound = val; // == b
                            c.upperBound = val;
                        }
                    }
                }
                break;
            }

            case MpsSection::BOUNDS: {
                std::string bound_type = token;
                std::string bound_label, var_name;
                ss >> bound_label >> var_name;

                size_t v_idx = get_or_create_variable(var_name, model);
                auto& var = model.variables[v_idx];
                double val = 0.0;

                if (bound_type == "LO") {
                    ss >> val;
                    var.lowerBound = val;
                } else if (bound_type == "UP") {
                    ss >> val;
                    var.upperBound = val;
                } else if (bound_type == "FX") {
                    ss >> val;
                    var.lowerBound = val;
                    var.upperBound = val;
                } else if (bound_type == "FR") {
                    var.lowerBound = -std::numeric_limits<double>::infinity();
                    var.upperBound = std::numeric_limits<double>::infinity();
                }
                break;
            }

            default:
                break;
        }
    }

    // Validate the built model before returning
    if (!model.validate()) {
        throw std::runtime_error("MpsReader Error: Parsed model failed model.validate()");
    }

    return model;
}

size_t MpsReader::get_or_create_variable(const std::string& var_name, model::Model& model) {
    auto it = var_name_to_idx_.find(var_name);
    if (it != var_name_to_idx_.end()) {
        return it->second;
    }

    size_t new_idx = model.variables.size();
    model::Variable var;
    var.name = var_name;
    var.type = model::VariableType::Continuous; // Continuous for Phase 1
    var.lowerBound = 0.0;
    var.upperBound = std::numeric_limits<double>::infinity();

    model.variables.push_back(var);
    var_name_to_idx_[var_name] = new_idx;
    return new_idx;
}

void MpsReader::add_coefficient(const std::string& var_name, const std::string& row_name, double value, model::Model& model) {
    get_or_create_variable(var_name, model);

    if (row_name == objective_row_name_) {
        model.objective.linearTerms[var_name] += value;
    } else {
        auto it = constraint_name_to_idx_.find(row_name);
        if (it != constraint_name_to_idx_.end()) {
            size_t c_idx = it->second;
            model.constraints[c_idx].linearTerms[var_name] += value;
        }
    }
}

} // namespace mps