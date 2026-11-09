#include <exception>
#include <iostream>

void run_async_request_cases();
void run_project_cases();
void run_editcommand_cases();
void run_workspace_lifecycle_cases();
void run_workspace_root_replacement_cases();
void run_preference_cases();
void run_editor_wait_schedule_cases();
void run_known_validation_cases();
void run_editor_action_and_style_cases();
void run_hierarchy_label_cache_cases();
void run_large_hierarchy_cache_cases();
void run_hierarchy_visible_connector_work_cases();

int main()
{
    try
    {
        run_async_request_cases();
        run_project_cases();
        run_editcommand_cases();
        run_workspace_lifecycle_cases();
        run_workspace_root_replacement_cases();
        run_preference_cases();
        run_editor_wait_schedule_cases();
        run_known_validation_cases();
        run_editor_action_and_style_cases();
        run_hierarchy_label_cache_cases();
        run_large_hierarchy_cache_cases();
        run_hierarchy_visible_connector_work_cases();
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_workspace_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
