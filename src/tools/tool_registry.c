#include "tool_registry.h"
#include "../../external/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ToolRegistry *tool_registry_create(void)
{
    ToolRegistry *registry = (ToolRegistry *)calloc(1, sizeof(ToolRegistry));
    if (!registry) return NULL;
    return registry;
}

void tool_registry_destroy(ToolRegistry *registry)
{
    if (registry) {
        free(registry);
    }
}

void tool_registry_add(ToolRegistry *registry, const ToolDefinition *tool)
{
    if (!registry || !tool) return;
    if (registry->tool_count >= MAX_TOOLS) return;

    memcpy(&registry->tools[registry->tool_count], tool, sizeof(ToolDefinition));
    registry->tool_count++;
}

static void add_param(ToolDefinition *tool, const char *name, const char *desc, ToolParamType type, bool required)
{
    if (!tool || tool->param_count >= MAX_TOOL_PARAMS) return;

    ToolParameter *param = &tool->params[tool->param_count];
    strncpy(param->name, name, 63);
    param->name[63] = '\0';
    strncpy(param->description, desc, 255);
    param->description[255] = '\0';
    param->type = type;
    param->required = required;
    tool->param_count++;
}

void tool_registry_register_file_tools(ToolRegistry *registry)
{
    if (!registry) return;

    // file_list - List directory contents
    ToolDefinition file_list = {0};
    strncpy(file_list.name, "file_list", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_list.description, "List files and directories in a path. Returns names, sizes, types, and modification dates.", TOOL_MAX_DESC_LEN - 1);
    file_list.requires_confirmation = false;
    add_param(&file_list, "path", "Directory path to list", TOOL_PARAM_STRING, true);
    add_param(&file_list, "show_hidden", "Include hidden files (default: false)", TOOL_PARAM_BOOLEAN, false);
    add_param(&file_list, "recursive", "List subdirectories recursively", TOOL_PARAM_BOOLEAN, false);
    tool_registry_add(registry, &file_list);

    // file_move - Move files
    ToolDefinition file_move = {0};
    strncpy(file_move.name, "file_move", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_move.description, "Move files or directories to a new location", TOOL_MAX_DESC_LEN - 1);
    file_move.requires_confirmation = true;
    add_param(&file_move, "source", "Source path or array of paths to move", TOOL_PARAM_STRING, true);
    add_param(&file_move, "destination", "Destination directory path", TOOL_PARAM_STRING, true);
    tool_registry_add(registry, &file_move);

    // file_copy - Copy files
    ToolDefinition file_copy = {0};
    strncpy(file_copy.name, "file_copy", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_copy.description, "Copy files or directories to a new location", TOOL_MAX_DESC_LEN - 1);
    file_copy.requires_confirmation = true;
    add_param(&file_copy, "source", "Source path or array of paths to copy", TOOL_PARAM_STRING, true);
    add_param(&file_copy, "destination", "Destination directory path", TOOL_PARAM_STRING, true);
    tool_registry_add(registry, &file_copy);

    // file_delete - Delete files (to trash)
    ToolDefinition file_delete = {0};
    strncpy(file_delete.name, "file_delete", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_delete.description, "Move files or directories to Trash", TOOL_MAX_DESC_LEN - 1);
    file_delete.requires_confirmation = true;
    add_param(&file_delete, "paths", "Path or array of paths to delete", TOOL_PARAM_ARRAY, true);
    tool_registry_add(registry, &file_delete);

    // file_create - Create new file or directory
    ToolDefinition file_create = {0};
    strncpy(file_create.name, "file_create", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_create.description, "Create a new file or directory", TOOL_MAX_DESC_LEN - 1);
    file_create.requires_confirmation = false;
    add_param(&file_create, "path", "Path for the new file or directory", TOOL_PARAM_STRING, true);
    add_param(&file_create, "is_directory", "Create a directory instead of file", TOOL_PARAM_BOOLEAN, false);
    add_param(&file_create, "content", "Initial content for files", TOOL_PARAM_STRING, false);
    tool_registry_add(registry, &file_create);

    // file_rename - Rename a file
    ToolDefinition file_rename = {0};
    strncpy(file_rename.name, "file_rename", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_rename.description, "Rename a file or directory", TOOL_MAX_DESC_LEN - 1);
    file_rename.requires_confirmation = true;
    add_param(&file_rename, "path", "Path of file to rename", TOOL_PARAM_STRING, true);
    add_param(&file_rename, "new_name", "New name for the file", TOOL_PARAM_STRING, true);
    tool_registry_add(registry, &file_rename);

    // file_search - Search for files
    ToolDefinition file_search = {0};
    strncpy(file_search.name, "file_search", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_search.description, "Search for files by name pattern or content", TOOL_MAX_DESC_LEN - 1);
    file_search.requires_confirmation = false;
    add_param(&file_search, "path", "Directory to search in", TOOL_PARAM_STRING, true);
    add_param(&file_search, "pattern", "Filename pattern (supports wildcards)", TOOL_PARAM_STRING, true);
    add_param(&file_search, "recursive", "Search subdirectories", TOOL_PARAM_BOOLEAN, false);
    tool_registry_add(registry, &file_search);

    // file_metadata - Get file information
    ToolDefinition file_metadata = {0};
    strncpy(file_metadata.name, "file_metadata", TOOL_MAX_NAME_LEN - 1);
    strncpy(file_metadata.description, "Get detailed metadata about a file or directory", TOOL_MAX_DESC_LEN - 1);
    file_metadata.requires_confirmation = false;
    add_param(&file_metadata, "path", "Path of file to inspect", TOOL_PARAM_STRING, true);
    tool_registry_add(registry, &file_metadata);

    // batch_rename - Rename multiple files with pattern
    ToolDefinition batch_rename = {0};
    strncpy(batch_rename.name, "batch_rename", TOOL_MAX_NAME_LEN - 1);
    strncpy(batch_rename.description, "Rename multiple files using a pattern", TOOL_MAX_DESC_LEN - 1);
    batch_rename.requires_confirmation = true;
    add_param(&batch_rename, "paths", "Array of file paths to rename", TOOL_PARAM_ARRAY, true);
    add_param(&batch_rename, "pattern", "Renaming pattern with placeholders", TOOL_PARAM_STRING, true);
    add_param(&batch_rename, "find", "Text to find in filenames", TOOL_PARAM_STRING, false);
    add_param(&batch_rename, "replace", "Text to replace found text with", TOOL_PARAM_STRING, false);
    tool_registry_add(registry, &batch_rename);

    // batch_move - Move multiple files to organized folders
    ToolDefinition batch_move = {0};
    strncpy(batch_move.name, "batch_move", TOOL_MAX_NAME_LEN - 1);
    strncpy(batch_move.description, "Move multiple files to destination, optionally organizing by type or date", TOOL_MAX_DESC_LEN - 1);
    batch_move.requires_confirmation = true;
    add_param(&batch_move, "paths", "Array of file paths to move", TOOL_PARAM_ARRAY, true);
    add_param(&batch_move, "destination", "Destination directory", TOOL_PARAM_STRING, true);
    add_param(&batch_move, "organize_by", "How to organize: 'type', 'date', or 'none'", TOOL_PARAM_STRING, false);
    tool_registry_add(registry, &batch_move);

    // semantic_search - AI-powered content search
    ToolDefinition semantic_search = {0};
    strncpy(semantic_search.name, "semantic_search", TOOL_MAX_NAME_LEN - 1);
    strncpy(semantic_search.description, "Search for files by their content meaning using AI embeddings. Find files based on description rather than exact text.", TOOL_MAX_DESC_LEN - 1);
    semantic_search.requires_confirmation = false;
    add_param(&semantic_search, "query", "Natural language description of what to find", TOOL_PARAM_STRING, true);
    add_param(&semantic_search, "directory", "Directory to search in (default: current directory)", TOOL_PARAM_STRING, false);
    add_param(&semantic_search, "max_results", "Maximum number of results to return (default: 20)", TOOL_PARAM_INTEGER, false);
    add_param(&semantic_search, "file_type", "Filter by file type: 'text', 'code', 'document', 'image'", TOOL_PARAM_STRING, false);
    tool_registry_add(registry, &semantic_search);

    // visual_search - AI-powered image search
    ToolDefinition visual_search = {0};
    strncpy(visual_search.name, "visual_search", TOOL_MAX_NAME_LEN - 1);
    strncpy(visual_search.description, "Search for images by natural language description using CLIP AI. Find photos matching descriptions like 'sunset at beach'.", TOOL_MAX_DESC_LEN - 1);
    visual_search.requires_confirmation = false;
    add_param(&visual_search, "query", "Natural language description of images to find", TOOL_PARAM_STRING, true);
    add_param(&visual_search, "directory", "Directory to search in (default: current directory)", TOOL_PARAM_STRING, false);
    add_param(&visual_search, "max_results", "Maximum number of results to return (default: 20)", TOOL_PARAM_INTEGER, false);
    tool_registry_add(registry, &visual_search);

    // similar_images - Find similar images
    ToolDefinition similar_images = {0};
    strncpy(similar_images.name, "similar_images", TOOL_MAX_NAME_LEN - 1);
    strncpy(similar_images.description, "Find images visually similar to a given image file using CLIP AI embeddings.", TOOL_MAX_DESC_LEN - 1);
    similar_images.requires_confirmation = false;
    add_param(&similar_images, "image_path", "Path to the reference image", TOOL_PARAM_STRING, true);
    add_param(&similar_images, "directory", "Directory to search in (default: current directory)", TOOL_PARAM_STRING, false);
    add_param(&similar_images, "max_results", "Maximum number of similar images to return (default: 20)", TOOL_PARAM_INTEGER, false);
    tool_registry_add(registry, &similar_images);

    // image_generate - Generate images with Gemini AI
    ToolDefinition image_generate = {0};
    strncpy(image_generate.name, "image_generate", TOOL_MAX_NAME_LEN - 1);
    strncpy(image_generate.description, "Generate an image from a text description using Gemini AI. The image will be saved to the current directory.", TOOL_MAX_DESC_LEN - 1);
    image_generate.requires_confirmation = false;
    add_param(&image_generate, "prompt", "Text description of the image to generate", TOOL_PARAM_STRING, true);
    add_param(&image_generate, "filename", "Name for the output file without extension (default: generated_image)", TOOL_PARAM_STRING, false);
    add_param(&image_generate, "model", "Model to use: 'fast' or 'quality' (default: fast)", TOOL_PARAM_STRING, false);
    tool_registry_add(registry, &image_generate);
}

const ToolDefinition *tool_registry_find(const ToolRegistry *registry, const char *name)
{
    if (!registry || !name) return NULL;

    for (int i = 0; i < registry->tool_count; i++) {
        if (strcmp(registry->tools[i].name, name) == 0) {
            return &registry->tools[i];
        }
    }
    return NULL;
}

int tool_registry_count(const ToolRegistry *registry)
{
    return registry ? registry->tool_count : 0;
}

void tool_result_init(ToolResult *result)
{
    if (!result) return;
    memset(result, 0, sizeof(ToolResult));
}

void tool_result_cleanup(ToolResult *result)
{
    if (!result) return;
    if (result->output) {
        free(result->output);
        result->output = NULL;
    }
    if (result->error) {
        free(result->error);
        result->error = NULL;
    }
}

void tool_result_set_success(ToolResult *result, const char *output, int affected)
{
    if (!result) return;
    result->success = true;
    result->exit_code = 0;
    result->affected_count = affected;
    if (output) {
        result->output = strdup(output);
    }
}

void tool_result_set_error(ToolResult *result, const char *error)
{
    if (!result) return;
    result->success = false;
    result->exit_code = 1;
    if (error) {
        result->error = strdup(error);
    }
}

const char *tool_param_type_to_string(ToolParamType type)
{
    switch (type) {
        case TOOL_PARAM_STRING: return "string";
        case TOOL_PARAM_INTEGER: return "integer";
        case TOOL_PARAM_BOOLEAN: return "boolean";
        case TOOL_PARAM_ARRAY: return "array";
        case TOOL_PARAM_OBJECT: return "object";
        default: return "string";
    }
}

bool tool_requires_confirmation(const ToolDefinition *tool)
{
    return tool ? tool->requires_confirmation : false;
}

cJSON *tool_definition_to_json(const ToolDefinition *tool)
{
    if (!tool) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "name", tool->name);
    cJSON_AddStringToObject(root, "description", tool->description);

    cJSON *input_schema = cJSON_CreateObject();
    cJSON_AddStringToObject(input_schema, "type", "object");

    cJSON *properties = cJSON_CreateObject();
    cJSON *required = cJSON_CreateArray();

    for (int i = 0; i < tool->param_count; i++) {
        const ToolParameter *param = &tool->params[i];

        cJSON *param_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(param_obj, "type", tool_param_type_to_string(param->type));
        cJSON_AddStringToObject(param_obj, "description", param->description);

        if (param->type == TOOL_PARAM_ARRAY) {
            cJSON *items = cJSON_CreateObject();
            cJSON_AddStringToObject(items, "type", "string");
            cJSON_AddItemToObject(param_obj, "items", items);
        }

        cJSON_AddItemToObject(properties, param->name, param_obj);

        if (param->required) {
            cJSON_AddItemToArray(required, cJSON_CreateString(param->name));
        }
    }

    cJSON_AddItemToObject(input_schema, "properties", properties);
    cJSON_AddItemToObject(input_schema, "required", required);
    cJSON_AddItemToObject(root, "input_schema", input_schema);

    return root;
}

cJSON *tool_registry_to_json(const ToolRegistry *registry)
{
    if (!registry) return NULL;

    cJSON *tools_array = cJSON_CreateArray();
    if (!tools_array) return NULL;

    for (int i = 0; i < registry->tool_count; i++) {
        cJSON *tool_json = tool_definition_to_json(&registry->tools[i]);
        if (tool_json) {
            cJSON_AddItemToArray(tools_array, tool_json);
        }
    }

    return tools_array;
}

char *tool_result_to_json(const ToolResult *result)
{
    if (!result) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddBoolToObject(root, "success", result->success);

    if (result->output) {
        cJSON_AddStringToObject(root, "output", result->output);
    }

    if (result->error) {
        cJSON_AddStringToObject(root, "error", result->error);
    }

    cJSON_AddNumberToObject(root, "exit_code", result->exit_code);
    cJSON_AddNumberToObject(root, "affected_count", result->affected_count);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json;
}
