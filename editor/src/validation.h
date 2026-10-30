#pragma once

#include "document.h"

#include <optional>
#include <string>
#include <vector>

namespace platformator_editor
{
enum class ValidationSeverity
{
    Warning,
    Error
};

struct ValidationIssue
{
    ValidationSeverity severity = ValidationSeverity::Error;
    std::optional<int> objectId;
    std::optional<int> componentId;
    std::string fieldPath;
    std::string message;
    bool blocksRun = false;
};

struct ValidationResult
{
    bool structurallyValid = true;
    bool blocksRun = false;
    std::vector<ValidationIssue> issues;

    bool valid_for_run() const noexcept
    {
        return structurallyValid && !blocksRun;
    }
};

ValidationResult validate_document(const DocumentSession &document);
} // namespace platformator_editor
