#include <fbxsdk.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

class FBXProcessor {
private:
    FbxManager* fbxManager;
    
    FbxScene* CreateNewScene() {
        FbxScene* scene = FbxScene::Create(fbxManager, "");
        return scene;
    }

    std::string GetActorNameFromNode(FbxNode* node) {
        std::string nodeName = node->GetName();
        for (size_t i = 0; i < nodeName.length(); i++) {
            if (!std::isalnum(nodeName[i])) {
                nodeName[i] = '_';
            }
        }
        return nodeName;
    }
    
    void FindSkeletons(FbxNode* node, std::vector<FbxNode*>& skeletons) {
        if (node->GetNodeAttribute()) {
            FbxNodeAttribute::EType attributeType = node->GetNodeAttribute()->GetAttributeType();
            
            if (attributeType == FbxNodeAttribute::eSkeleton) {
                bool isRoot = true;
                FbxNode* parent = node->GetParent();
                while (parent) {
                    if (parent->GetNodeAttribute() && 
                        parent->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
                        isRoot = false;
                        break;
                    }
                    parent = parent->GetParent();
                }
                
                if (isRoot) {
                    skeletons.push_back(node);
                }
            }
        }
        
        for (int i = 0; i < node->GetChildCount(); i++) {
            FindSkeletons(node->GetChild(i), skeletons);
        }
    }
    
    FbxScene* ExtractSkeleton(FbxScene* originalScene, FbxNode* skeletonRoot) {
        FbxScene* newScene = CreateNewScene();
        
        newScene->GetGlobalSettings().SetAxisSystem(originalScene->GetGlobalSettings().GetAxisSystem());
        newScene->GetGlobalSettings().SetSystemUnit(originalScene->GetGlobalSettings().GetSystemUnit());
        
        FbxNode* newRoot = CloneNodeHierarchy(skeletonRoot, newScene->GetRootNode(), originalScene, newScene);

        auto AttachSkinnedMeshes = [&](FbxScene* srcScene, FbxScene* dstScene, FbxNode* skelRoot, FbxNode* dstRoot) {
            int nodeCount = srcScene->GetNodeCount();
            for (int i = 0; i < nodeCount; ++i) {
                FbxNode* node = srcScene->GetNode(i);
                FbxMesh* mesh = node->GetMesh();
                    if (!mesh) continue;

                bool linked = false;
                for (int deformerIndex = 0; deformerIndex < mesh->GetDeformerCount(); ++deformerIndex) {
                    FbxSkin* skin = FbxCast<FbxSkin>(mesh->GetDeformer(deformerIndex, FbxDeformer::eSkin));
                        if (!skin) continue;

                    for (int clusterIndex = 0; clusterIndex < skin->GetClusterCount(); ++clusterIndex) {
                        FbxCluster* cluster = skin->GetCluster(clusterIndex);
                        FbxNode* link = cluster->GetLink();
                            if (link && link->GetName() == skelRoot->GetName()) {
                                linked = true;
                                break;                   
                            }
                    }
                }

                if (linked) {
                    FbxNode* clonedMeshNode = CloneNodeHierarchy(node, dstRoot, srcScene, dstScene);
                    std::cout << "  Attached mesh: " << node->GetName() << std::endl;
                }
            }
        };

        AttachSkinnedMeshes(originalScene, newScene, skeletonRoot, newRoot);
  
        CopyAnimation(originalScene, newScene, skeletonRoot, newRoot);
        
        return newScene;
    }
    
    FbxNode* CloneNodeHierarchy(FbxNode* sourceNode, FbxNode* destParent, FbxScene* sourceScene, FbxScene* destScene) {
        if (!sourceNode) return nullptr;
        
        FbxNode* newNode = FbxNode::Create(destScene, sourceNode->GetName());
        destParent->AddChild(newNode);
        
        if (sourceNode->GetNodeAttribute()) {
            FbxNodeAttribute* originalAttribute = sourceNode->GetNodeAttribute();
            FbxNodeAttribute::EType attributeType = originalAttribute->GetAttributeType();
            
            if (attributeType == FbxNodeAttribute::eSkeleton) {
                FbxSkeleton* skeleton = FbxSkeleton::Create(destScene, "");
                skeleton->SetSkeletonType(((FbxSkeleton*)originalAttribute)->GetSkeletonType());
                newNode->SetNodeAttribute(skeleton);
            } else if (attributeType == FbxNodeAttribute::eMesh) {
                FbxMesh* originalMesh = (FbxMesh*)originalAttribute;
                FbxMesh* newMesh = FbxMesh::Create(destScene, originalMesh->GetName());
                
                newMesh->InitControlPoints(originalMesh->GetControlPointsCount());
                FbxVector4* controlPoints = newMesh->GetControlPoints();
                FbxVector4* originalControlPoints = originalMesh->GetControlPoints();
                
                for (int i = 0; i < originalMesh->GetControlPointsCount(); i++) {
                    controlPoints[i] = originalControlPoints[i];
                }
                
                for (int i = 0; i < originalMesh->GetPolygonCount(); i++) {
                    int polygonSize = originalMesh->GetPolygonSize(i);
                    newMesh->BeginPolygon();
                    
                    for (int j = 0; j < polygonSize; j++) {
                        int index = originalMesh->GetPolygonVertex(i, j);
                        newMesh->AddPolygon(index);
                    }
                    
                    newMesh->EndPolygon();
                }
                
                newNode->SetNodeAttribute(newMesh);
            }
        }
        
        newNode->LclTranslation.Set(sourceNode->LclTranslation.Get());
        newNode->LclRotation.Set(sourceNode->LclRotation.Get());
        newNode->LclScaling.Set(sourceNode->LclScaling.Get());
        
        for (int i = 0; i < sourceNode->GetChildCount(); i++) {
            CloneNodeHierarchy(sourceNode->GetChild(i), newNode, sourceScene, destScene);
        }
        
        return newNode;
    }
    
    void CopyAnimation(FbxScene* sourceScene, FbxScene* destScene, FbxNode* sourceNode, FbxNode* destNode) {
        int animStackCount = sourceScene->GetSrcObjectCount<FbxAnimStack>();
        
        for (int stackIndex = 0; stackIndex < animStackCount; stackIndex++) {
            FbxAnimStack* sourceStack = sourceScene->GetSrcObject<FbxAnimStack>(stackIndex);
            
            FbxAnimStack* destStack = FbxAnimStack::Create(destScene, sourceStack->GetName());
            
            int layerCount = sourceStack->GetMemberCount<FbxAnimLayer>();
            
            for (int layerIndex = 0; layerIndex < layerCount; layerIndex++) {
                FbxAnimLayer* sourceLayer = sourceStack->GetMember<FbxAnimLayer>(layerIndex);
                FbxAnimLayer* destLayer = FbxAnimLayer::Create(destScene, sourceLayer->GetName());
                destStack->AddMember(destLayer);
                
                CopyNodeAnimation(sourceNode, destNode, sourceLayer, destLayer);
            }
        }
    }
    
    void CopyNodeAnimation(FbxNode* sourceNode, FbxNode* destNode, FbxAnimLayer* sourceLayer, FbxAnimLayer* destLayer) {
        CopyAnimationCurve(sourceNode->LclTranslation.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_X), 
                           destNode->LclTranslation.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_X, true));
        CopyAnimationCurve(sourceNode->LclTranslation.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_Y), 
                           destNode->LclTranslation.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_Y, true));
        CopyAnimationCurve(sourceNode->LclTranslation.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_Z), 
                           destNode->LclTranslation.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_Z, true));
        
        CopyAnimationCurve(sourceNode->LclRotation.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_X), 
                           destNode->LclRotation.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_X, true));
        CopyAnimationCurve(sourceNode->LclRotation.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_Y), 
                           destNode->LclRotation.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_Y, true));
        CopyAnimationCurve(sourceNode->LclRotation.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_Z), 
                           destNode->LclRotation.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_Z, true));
        
        CopyAnimationCurve(sourceNode->LclScaling.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_X), 
                           destNode->LclScaling.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_X, true));
        CopyAnimationCurve(sourceNode->LclScaling.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_Y), 
                           destNode->LclScaling.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_Y, true));
        CopyAnimationCurve(sourceNode->LclScaling.GetCurve(sourceLayer, FBXSDK_CURVENODE_COMPONENT_Z), 
                           destNode->LclScaling.GetCurve(destLayer, FBXSDK_CURVENODE_COMPONENT_Z, true));
        
        for (int i = 0; i < sourceNode->GetChildCount() && i < destNode->GetChildCount(); i++) {
            CopyNodeAnimation(sourceNode->GetChild(i), destNode->GetChild(i), sourceLayer, destLayer);
        }
    }
    
    void CopyAnimationCurve(FbxAnimCurve* sourceCurve, FbxAnimCurve* destCurve) {
        if (!sourceCurve || !destCurve) return;
        
        int keyCount = sourceCurve->KeyGetCount();
        
        for (int keyIndex = 0; keyIndex < keyCount; keyIndex++) {
            FbxTime keyTime = sourceCurve->KeyGetTime(keyIndex);
            float keyValue = sourceCurve->KeyGetValue(keyIndex);
            
            int destKeyIndex = destCurve->KeyAdd(keyTime);
            destCurve->KeySetValue(destKeyIndex, keyValue);
            destCurve->KeySetInterpolation(destKeyIndex, sourceCurve->KeyGetInterpolation(keyIndex));
        }
    }
    
    void CenterActor(FbxScene* scene, bool rotateToFaceZ = false) {
        FbxNode* rootNode = scene->GetRootNode();
        std::vector<FbxNode*> skeletons;
        FindSkeletons(rootNode, skeletons);
        
        if (skeletons.empty()) {
            std::cerr << "No skeletons found in the scene!" << std::endl;
            return;
        }
        
        FbxNode* skeletonRoot = skeletons[0];
        
        FbxDouble3 initialPosition = skeletonRoot->LclTranslation.Get();
        
        FbxDouble3 newPosition = FbxDouble3(0.0, initialPosition[1], 0.0);
        FbxDouble3 translationOffset = FbxDouble3(
            -initialPosition[0],
            0.0,  // keep Y unchanged
            -initialPosition[2]
        );
        
        ApplyTranslationToNodeAndAnimation(skeletonRoot, translationOffset, scene);
        
        // optionally rotate to face positive Z direction
        if (rotateToFaceZ) {
            
            FbxNode* hipNode = skeletonRoot; // the root node or hip node is the reference ?
            FbxDouble3 rotation = hipNode->LclRotation.Get();
            
            // rotation to face positive Z should set absolute rotation to 0 degrees around Y axis ?
            // FbxDouble3 newRotation = FbxDouble3(rotation[0], 0.0, rotation[2]);
            // FbxDouble3 rotationOffset = FbxDouble3(0.0, -rotation[1], 0.0);
            

            // forward vector estimaiton based on a child node (e.g., hips to spine or hips to head) (fix?)
            FbxDouble3 rotationOffset;
            FbxNode* spineNode = nullptr;
            for (int i = 0; i < hipNode->GetChildCount(); ++i) {
                FbxNode* child = hipNode->GetChild(i);
                if (child->GetName() && std::string(child->GetName()).find("Spine") != std::string::npos) {
                    spineNode = child;
                    break;
                }
            }
            if (!spineNode && hipNode->GetChildCount() > 0) {
                spineNode = hipNode->GetChild(0); // fallback
            }

            if (spineNode) {
                FbxVector4 hipPos = hipNode->EvaluateGlobalTransform().GetT();
                FbxVector4 spinePos = spineNode->EvaluateGlobalTransform().GetT();

                FbxVector4 forward = spinePos - hipPos;
                double angle = atan2(forward[0], forward[2]) * 180.0 / 3.141592653589793;

                rotationOffset = FbxDouble3(0.0, -angle, 0.0);
                ApplyRotationToNodeAndAnimation(hipNode, rotationOffset, scene);
            }

            ApplyRotationToNodeAndAnimation(hipNode, rotationOffset, scene);
        }
    }
    
    void ApplyTranslationToNodeAndAnimation(FbxNode* node, const FbxDouble3& translationOffset, FbxScene* scene) {
        FbxDouble3 currentTranslation = node->LclTranslation.Get();
        
        FbxDouble3 newTranslation = FbxDouble3(
            currentTranslation[0] + translationOffset[0],
            currentTranslation[1] + translationOffset[1],
            currentTranslation[2] + translationOffset[2]
        );
        
        // setting the new static translation does not work ?
        // node->LclTranslation.Set(newTranslation);

        int animStackCount = scene->GetSrcObjectCount<FbxAnimStack>();
        
        for (int stackIndex = 0; stackIndex < animStackCount; stackIndex++) {
            FbxAnimStack* animStack = scene->GetSrcObject<FbxAnimStack>(stackIndex);
            scene->SetCurrentAnimationStack(animStack);
            
            int layerCount = animStack->GetMemberCount<FbxAnimLayer>();
            
            for (int layerIndex = 0; layerIndex < layerCount; layerIndex++) {
                FbxAnimLayer* animLayer = animStack->GetMember<FbxAnimLayer>(layerIndex);

                FbxAnimCurve* xCurve = node->LclTranslation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_X);
                if (xCurve) {
                    for (int keyIndex = 0; keyIndex < xCurve->KeyGetCount(); keyIndex++) {
                        float keyValue = xCurve->KeyGetValue(keyIndex);
                        xCurve->KeySetValue(keyIndex, keyValue + translationOffset[0]);
                    }
                }

                FbxAnimCurve* zCurve = node->LclTranslation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Z);
                if (zCurve) {
                    for (int keyIndex = 0; keyIndex < zCurve->KeyGetCount(); keyIndex++) {
                        float keyValue = zCurve->KeyGetValue(keyIndex);
                        zCurve->KeySetValue(keyIndex, keyValue + translationOffset[2]);
                    }
                }
            }
        }
    }
    
    void ApplyRotationToNodeAndAnimation(FbxNode* node, const FbxDouble3& rotationOffset, FbxScene* scene) {
        FbxDouble3 currentRotation = node->LclRotation.Get();
        
        FbxDouble3 newRotation = FbxDouble3(
            currentRotation[0] + rotationOffset[0],
            currentRotation[1] + rotationOffset[1],
            currentRotation[2] + rotationOffset[2]
        );
        
        node->LclRotation.Set(newRotation);
        
        int animStackCount = scene->GetSrcObjectCount<FbxAnimStack>();
        
        for (int stackIndex = 0; stackIndex < animStackCount; stackIndex++) {
            FbxAnimStack* animStack = scene->GetSrcObject<FbxAnimStack>(stackIndex);
            scene->SetCurrentAnimationStack(animStack);
            
            int layerCount = animStack->GetMemberCount<FbxAnimLayer>();
            
            for (int layerIndex = 0; layerIndex < layerCount; layerIndex++) {
                FbxAnimLayer* animLayer = animStack->GetMember<FbxAnimLayer>(layerIndex);
                
                FbxAnimCurve* yCurve = node->LclRotation.GetCurve(animLayer, FBXSDK_CURVENODE_COMPONENT_Y);
                if (yCurve) {
                    for (int keyIndex = 0; keyIndex < yCurve->KeyGetCount(); keyIndex++) {
                        float keyValue = yCurve->KeyGetValue(keyIndex);
                        yCurve->KeySetValue(keyIndex, keyValue + rotationOffset[1]);
                    }
                }
            }
        }
    }

public:
    FBXProcessor() {
        fbxManager = FbxManager::Create();
        
        FbxIOSettings* ios = FbxIOSettings::Create(fbxManager, IOSROOT);
        fbxManager->SetIOSettings(ios);
    }
    
    ~FBXProcessor() {
        fbxManager->Destroy();
    }
    
    void ProcessFile(const std::string& inputFilePath, bool rotateToFaceZ = false) {
        std::cout << "Processing: " << inputFilePath << std::endl;
        
        FbxImporter* importer = FbxImporter::Create(fbxManager, "");
        
        if (!importer->Initialize(inputFilePath.c_str(), -1, fbxManager->GetIOSettings())) {
            std::cerr << "Failed to initialize importer: " << importer->GetStatus().GetErrorString() << std::endl;
            importer->Destroy();
            return;
        }
        
        FbxScene* scene = FbxScene::Create(fbxManager, "importScene");
        
        if (!importer->Import(scene)) {
            std::cerr << "Failed to import scene: " << importer->GetStatus().GetErrorString() << std::endl;
            importer->Destroy();
            scene->Destroy();
            return;
        }
        
        importer->Destroy();
        
        std::vector<FbxNode*> skeletons;
        FindSkeletons(scene->GetRootNode(), skeletons);
        
        std::cout << "Found " << skeletons.size() << " skeletons in the file." << std::endl;
        
        fs::path inputPath(inputFilePath);
        std::string baseFileName = inputPath.stem().string();
        std::string outputDir = inputPath.parent_path().string();
        
        for (FbxNode* skeleton : skeletons) {
            std::string actorName = GetActorNameFromNode(skeleton);
            std::cout << "  Processing skeleton: " << actorName << std::endl;
            
            FbxScene* newScene = ExtractSkeleton(scene, skeleton);
            
            CenterActor(newScene, rotateToFaceZ);
            
            std::string outputFileName = baseFileName + "_" + actorName + ".fbx";
            std::string outputFilePath = (fs::path(outputDir) / outputFileName).string();
            
            FbxExporter* exporter = FbxExporter::Create(fbxManager, "");
            
            if (!exporter->Initialize(outputFilePath.c_str(), -1, fbxManager->GetIOSettings())) {
                std::cerr << "Failed to initialize exporter: " << exporter->GetStatus().GetErrorString() << std::endl;
                exporter->Destroy();
                newScene->Destroy();
                continue;
            }
            
            if (!exporter->Export(newScene)) {
                std::cerr << "Failed to export scene: " << exporter->GetStatus().GetErrorString() << std::endl;
            } else {
                std::cout << "  Successfully exported: " << outputFilePath << std::endl;
            }
            
            exporter->Destroy();
            newScene->Destroy();
        }
        
        scene->Destroy();
    }
    
    void ProcessDirectory(const std::string& directoryPath, bool rotateToFaceZ = false) {
        try {
            std::cout << "Processing directory: " << directoryPath << std::endl;
            
            for (const auto& entry : fs::directory_iterator(directoryPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".fbx") {
                    ProcessFile(entry.path().string(), rotateToFaceZ);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing directory: " << e.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <directory_path> [rotate_to_face_z]" << std::endl;
        std::cout << "  directory_path: Path to directory containing FBX files" << std::endl;
        std::cout << "  rotate_to_face_z: Optional flag (0 or 1) to rotate actors to face Z direction" << std::endl;
        return 1;
    }
    
    std::string directoryPath = argv[1];
    bool rotateToFaceZ = (argc > 2) ? (std::string(argv[2]) == "1") : false;
    
    try {
        FBXProcessor processor;
        processor.ProcessDirectory(directoryPath, rotateToFaceZ);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Processing complete!" << std::endl;
    return 0;
}