#pragma once

#include "behaviorcatalog.h"
#include "editcommands.h"
#include "panels/behaviorfields.h"
#include "selection.h"

namespace platformator_editor
{
class BehaviorsPanel
{
  public:
    void draw(DocumentSession &document, SelectionModel &selection,
              EditCommandService &commands, const BehaviorCatalog &catalog,
              const AssetBrowseCallback &browse = {});

  private:
    BehaviorFieldsPanel fields_;
};
} // namespace platformator_editor
