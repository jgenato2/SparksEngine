#include <fbxsdk.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: fbx_list_animstacks <file.fbx>\n";
        return 1;
    }


    // Print FBX SDK version (for FBX SDK 2020+)
    int major, minor, revision;
    FbxManager::GetFileFormatVersion(major, minor, revision);
    std::cout << "FBX SDK version: " << major << "." << minor << "." << revision << std::endl;

    // Print the file path being used
    std::cout << "Input path: " << argv[1] << std::endl;

    // Check if file exists and is readable
    FILE* testFile = fopen(argv[1], "rb");
    if (!testFile) {
        std::cout << "ERROR: File does not exist or cannot be opened: " << argv[1] << std::endl;
        return 1;
    } else {
        fclose(testFile);
    }

    // Initialize the FBX SDK manager and scene
    FbxManager* manager = FbxManager::Create();
    FbxIOSettings* ios = FbxIOSettings::Create(manager, IOSROOT);
    manager->SetIOSettings(ios);

    FbxImporter* importer = FbxImporter::Create(manager, "");
    if (!importer->Initialize(argv[1], -1, manager->GetIOSettings())) {
        std::cout << "Failed to open FBX file: " << importer->GetStatus().GetErrorString() << std::endl;
        std::cout << "Error code: " << importer->GetStatus().GetCode() << std::endl;
        return 1;
    }

    FbxScene* scene = FbxScene::Create(manager, "scene");
    if (!importer->Import(scene)) {
        std::cout << "Failed to import FBX scene: " << importer->GetStatus().GetErrorString() << std::endl;
        importer->Destroy();
        manager->Destroy();
        return 1;
    }
    importer->Destroy();

    // List all animation stacks (takes)
    int animStackCount = scene->GetSrcObjectCount<FbxAnimStack>();
    std::cout << "Animation Stacks (Takes) found: " << animStackCount << "\n";
    for (int i = 0; i < animStackCount; ++i) {
        FbxAnimStack* stack = scene->GetSrcObject<FbxAnimStack>(i);
        std::cout << "  [" << i << "] " << stack->GetName() << "\n";
    }

    // Cleanup
    manager->Destroy();
    return 0;
}
