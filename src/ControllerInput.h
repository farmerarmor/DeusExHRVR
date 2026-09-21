#pragma once
#include "SharedPair.h"
#include "GamepadMapping.h"
#include <openxr/openxr.h>
#include <cstring>
#include <vector>

class ControllerInput {
    enum Id {Aim,LeftStick,RightStick,LeftTrigger,RightTrigger,LeftGrip,RightGrip,A,B,X,Y,LeftClick,RightClick,Menu,Count};
    XrActionSet actions{};
    XrAction action[Count]{};
    XrSpace aimSpace{};
    GamepadMapping::Mapper mapper;
public:
    void Reset() {
        if(aimSpace)xrDestroySpace(aimSpace);
        if(actions)xrDestroyActionSet(actions);
        aimSpace=XR_NULL_HANDLE;actions=XR_NULL_HANDLE;
        for(auto& a:action)a=XR_NULL_HANDLE;
        mapper.Reset();
    }
    void SetLeftClickDelayMs(uint64_t ms){mapper.SetLeftClickDelay(ms);}
    bool Init(XrInstance instance,XrSession session) {
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(set.actionSetName,"motion_controller");strcpy_s(set.localizedActionSetName,"VR gamepad and weapon");
        if(XR_FAILED(xrCreateActionSet(instance,&set,&actions)))return false;
        struct Binding {const char* name;XrActionType type;const char* path;};
        const Binding bindings[Count]={
            {"right_weapon_aim",XR_ACTION_TYPE_POSE_INPUT,"/user/hand/right/input/aim/pose"},
            {"left_stick",XR_ACTION_TYPE_VECTOR2F_INPUT,"/user/hand/left/input/thumbstick"},
            {"right_stick",XR_ACTION_TYPE_VECTOR2F_INPUT,"/user/hand/right/input/thumbstick"},
            {"left_trigger",XR_ACTION_TYPE_FLOAT_INPUT,"/user/hand/left/input/trigger/value"},
            {"right_trigger",XR_ACTION_TYPE_FLOAT_INPUT,"/user/hand/right/input/trigger/value"},
            {"left_grip",XR_ACTION_TYPE_FLOAT_INPUT,"/user/hand/left/input/squeeze/value"},
            {"right_grip",XR_ACTION_TYPE_FLOAT_INPUT,"/user/hand/right/input/squeeze/value"},
            {"button_a",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/right/input/a/click"},
            {"button_b",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/right/input/b/click"},
            {"button_x",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/left/input/x/click"},
            {"button_y",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/left/input/y/click"},
            {"left_click",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/left/input/thumbstick/click"},
            {"right_click",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/right/input/thumbstick/click"},
            {"menu",XR_ACTION_TYPE_BOOLEAN_INPUT,"/user/hand/left/input/menu/click"}
        };
        std::vector<XrActionSuggestedBinding> suggested;
        for(int i=0;i<Count;i++) {
            XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};info.actionType=bindings[i].type;
            strcpy_s(info.actionName,bindings[i].name);strcpy_s(info.localizedActionName,bindings[i].name);
            if(XR_FAILED(xrCreateAction(actions,&info,&action[i])))return false;
            XrPath path{};if(XR_FAILED(xrStringToPath(instance,bindings[i].path,&path)))return false;
            suggested.push_back({action[i],path});
        }
        XrPath profile{};
        if(XR_FAILED(xrStringToPath(instance,"/interaction_profiles/oculus/touch_controller",&profile)))return false;
        XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestion.interactionProfile=profile;suggestion.countSuggestedBindings=static_cast<uint32_t>(suggested.size());suggestion.suggestedBindings=suggested.data();
        if(XR_FAILED(xrSuggestInteractionProfileBindings(instance,&suggestion)))return false;
        // Retain basic aim/menu support for runtimes exposing the simple profile.
        if(XR_SUCCEEDED(xrStringToPath(instance,"/interaction_profiles/khr/simple_controller",&profile))) {
            XrActionSuggestedBinding simple[]{suggested[Aim],suggested[Menu]};
            suggestion.interactionProfile=profile;suggestion.countSuggestedBindings=2;suggestion.suggestedBindings=simple;
            xrSuggestInteractionProfileBindings(instance,&suggestion);
        }
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets=1;attach.actionSets=&actions;
        if(XR_FAILED(xrAttachSessionActionSets(session,&attach)))return false;
        XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};space.action=action[Aim];space.poseInActionSpace.orientation.w=1;
        return XR_SUCCEEDED(xrCreateActionSpace(session,&space,&aimSpace));
    }
    void Sample(XrSession session,XrSpace local,XrTime time,bool focused,Transport::Tracking& tracking) {
        tracking.rightController={};tracking.gamepad={};tracking.controllerButtons=0;
        if(!aimSpace || !focused){mapper.Reset();return;}
        XrActiveActionSet active{actions,XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
        if(xrSyncActions(session,&sync)!=XR_SUCCESS){mapper.Reset();return;}
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action[Aim];
        XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
        if(XR_SUCCEEDED(xrGetActionStatePose(session,&info,&pose)) && pose.isActive) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if(XR_SUCCEEDED(xrLocateSpace(aimSpace,local,time,&location)) && (location.locationFlags&3)==3) {
                memcpy(&tracking.rightController.aim,&location.pose,sizeof(Transport::Pose));tracking.rightController.valid=1;
            }
        }
        GamepadMapping::Input input;
        auto boolean=[&](Id id) {
            info.action=action[id];XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            if(XR_FAILED(xrGetActionStateBoolean(session,&info,&state)) || !state.isActive)return false;
            input.active=true;return state.currentState!=0;
        };
        auto scalar=[&](Id id) {
            info.action=action[id];XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
            if(XR_FAILED(xrGetActionStateFloat(session,&info,&state)) || !state.isActive)return 0.f;
            input.active=true;return state.currentState;
        };
        auto stick=[&](Id id,float& x,float& y) {
            info.action=action[id];XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
            if(XR_SUCCEEDED(xrGetActionStateVector2f(session,&info,&state)) && state.isActive){input.active=true;x=state.currentState.x;y=state.currentState.y;}
        };
        input.a=boolean(A);input.b=boolean(B);input.x=boolean(X);input.y=boolean(Y);
        input.leftClick=boolean(LeftClick);input.rightClick=boolean(RightClick);input.menu=boolean(Menu);
        input.leftTrigger=scalar(LeftTrigger);input.rightTrigger=scalar(RightTrigger);
        input.leftGrip=scalar(LeftGrip);input.rightGrip=scalar(RightGrip);
        stick(LeftStick,input.leftX,input.leftY);stick(RightStick,input.rightX,input.rightY);
        tracking.controllerButtons=input.active?((input.leftClick?1u:0u)|(input.rightClick?2u:0u)):0u;
        tracking.gamepad=mapper.Update(input,tracking.tick);
    }
};
