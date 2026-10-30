#pragma once

#include "behaviorcatalog.h"
#include "editcommands.h"
#include "referencefields.h"
#include "selection.h"

namespace platformator_editor
{
class BehaviorFieldsPanel
{
  public:
    void draw(DocumentSession &document, SelectionModel &selection,
              EditCommandService &commands, const BehaviorCatalog &catalog,
              const AssetBrowseCallback &browse = {});
};
} // namespace platformator_editor
