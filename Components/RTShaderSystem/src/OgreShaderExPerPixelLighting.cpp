/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreShaderPrecompiledHeaders.h"
#ifdef RTSHADER_SYSTEM_BUILD_EXT_SHADERS

namespace Ogre {
namespace RTShader {

/************************************************************************/
/*                                                                      */
/************************************************************************/
String PerPixelLighting::Type = "SGX_PerPixelLighting";
Light PerPixelLighting::msBlankLight;

//-----------------------------------------------------------------------
PerPixelLighting::PerPixelLighting()
{
    mTrackVertexColourType          = TVC_NONE; 
    mSpecularEnable                 = false;

    msBlankLight.setDiffuseColour(ColourValue::Black);
    msBlankLight.setSpecularColour(ColourValue::Black);
    msBlankLight.setAttenuation(0,1,0,0);
}

//-----------------------------------------------------------------------
const String& PerPixelLighting::getType() const
{
    return Type;
}


//-----------------------------------------------------------------------
int PerPixelLighting::getExecutionOrder() const
{
    return FFP_LIGHTING;
}

//-----------------------------------------------------------------------
void PerPixelLighting::updateGpuProgramsParams(Renderable* rend, Pass* pass, const AutoParamDataSource* source, 
    const LightList* pLightList)
{
    if (mLightParamsList.empty())
        return;

    const Affine3& matView = source->getViewMatrix();
    Light::LightTypes curLightType = Light::LT_DIRECTIONAL; 
    unsigned int curSearchLightIndex = 0;

    // Update per light parameters.
    for (unsigned int i=0; i < mLightParamsList.size(); ++i)
    {
        const LightParams& curParams = mLightParamsList[i];

        if (curLightType != curParams.mType)
        {
            curLightType = curParams.mType;
            curSearchLightIndex = 0;
        }

        Light*      srcLight = NULL;
        Vector4     vParameter;
        ColourValue colour;

        // Search a matching light from the current sorted lights of the given renderable.
        for (unsigned int j = curSearchLightIndex; j < pLightList->size(); ++j)
        {
            if (pLightList->at(j)->getType() == curLightType)
            {               
                srcLight = pLightList->at(j);
                curSearchLightIndex = j + 1;
                break;
            }           
        }

        // No matching light found -> use a blank dummy light for parameter update.
        if (srcLight == NULL)
        {                       
            srcLight = &msBlankLight;
        }


        switch (curParams.mType)
        {
        case Light::LT_DIRECTIONAL:

            // Update light direction.
            vParameter = matView * srcLight->getAs4DVector(true);
            curParams.mDirection->setGpuParameter(vParameter);
            break;

        case Light::LT_POINT:

            // Update light position.
            vParameter = matView * srcLight->getAs4DVector(true);
            curParams.mPosition->setGpuParameter(vParameter);

            // Update light attenuation parameters.
            vParameter.x = srcLight->getAttenuationRange();
            vParameter.y = srcLight->getAttenuationConstant();
            vParameter.z = srcLight->getAttenuationLinear();
            vParameter.w = srcLight->getAttenuationQuadric();
            curParams.mAttenuatParams->setGpuParameter(vParameter);
            break;

        case Light::LT_SPOTLIGHT:
            {                       
                Vector3 vec3;
                
                // Update light position.
                vParameter = matView * srcLight->getAs4DVector(true);
                curParams.mPosition->setGpuParameter(vParameter);


                // Update light direction.
                vec3 = source->getInverseTransposeViewMatrix().linear() * srcLight->getDerivedDirection();
                vec3.normalise();

                vParameter.x = -vec3.x;
                vParameter.y = -vec3.y;
                vParameter.z = -vec3.z;
                vParameter.w = 0.0;
                curParams.mDirection->setGpuParameter(vParameter);

                // Update light attenuation parameters.
                vParameter.x = srcLight->getAttenuationRange();
                vParameter.y = srcLight->getAttenuationConstant();
                vParameter.z = srcLight->getAttenuationLinear();
                vParameter.w = srcLight->getAttenuationQuadric();
                curParams.mAttenuatParams->setGpuParameter(vParameter);

                // Update spotlight parameters.
                Real phi   = Math::Cos(srcLight->getSpotlightOuterAngle().valueRadians() * 0.5f);
                Real theta = Math::Cos(srcLight->getSpotlightInnerAngle().valueRadians() * 0.5f);

                vec3.x = theta;
                vec3.y = phi;
                vec3.z = srcLight->getSpotlightFalloff();

                curParams.mSpotParams->setGpuParameter(vec3);
            }
            break;
        }


        // Update diffuse colour.
        if ((mTrackVertexColourType & TVC_DIFFUSE) == 0)
        {
            colour = srcLight->getDiffuseColour() * pass->getDiffuse() * srcLight->getPowerScale();
            curParams.mDiffuseColour->setGpuParameter(colour);                  
        }
        else
        {                   
            colour = srcLight->getDiffuseColour() * srcLight->getPowerScale();
            curParams.mDiffuseColour->setGpuParameter(colour);  
        }

        // Update specular colour if need to.
        if (mSpecularEnable)
        {
            // Update diffuse colour.
            if ((mTrackVertexColourType & TVC_SPECULAR) == 0)
            {
                colour = srcLight->getSpecularColour() * pass->getSpecular() * srcLight->getPowerScale();
                curParams.mSpecularColour->setGpuParameter(colour);                 
            }
            else
            {                   
                colour = srcLight->getSpecularColour() * srcLight->getPowerScale();
                curParams.mSpecularColour->setGpuParameter(colour); 
            }
        }                                                                           
    }
}

//-----------------------------------------------------------------------
bool PerPixelLighting::resolveParameters(ProgramSet* programSet)
{
    if (false == resolveGlobalParameters(programSet))
        return false;
    
    if (false == resolvePerLightParameters(programSet))
        return false;
    
    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::resolveGlobalParameters(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain = vsProgram->getEntryPointFunction();
    Function* psMain = psProgram->getEntryPointFunction();
    bool hasError = false;
    // Resolve world view IT matrix.
    mWorldViewITMatrix = vsProgram->resolveParameter(GpuProgramParameters::ACT_INVERSE_TRANSPOSE_WORLDVIEW_MATRIX);
    hasError |= !(mWorldViewITMatrix.get());    
    
    // Get surface ambient colour if need to.
    if ((mTrackVertexColourType & TVC_AMBIENT) == 0)
    {       
        mDerivedAmbientLightColour = psProgram->resolveParameter(GpuProgramParameters::ACT_DERIVED_AMBIENT_LIGHT_COLOUR);
        hasError |= !(mDerivedAmbientLightColour.get());        
    }
    else
    {
        mLightAmbientColour = psProgram->resolveParameter(GpuProgramParameters::ACT_AMBIENT_LIGHT_COLOUR);
        mSurfaceAmbientColour = psProgram->resolveParameter(GpuProgramParameters::ACT_SURFACE_AMBIENT_COLOUR);
        
        hasError |= !(mSurfaceAmbientColour.get()) || !(mLightAmbientColour.get()); 
    }

    // Get surface diffuse colour if need to.
    if ((mTrackVertexColourType & TVC_DIFFUSE) == 0)
    {
        mSurfaceDiffuseColour = psProgram->resolveParameter(GpuProgramParameters::ACT_SURFACE_DIFFUSE_COLOUR);
        hasError |= !(mSurfaceDiffuseColour.get()); 
    }

    // Get surface specular colour if need to.
    if ((mTrackVertexColourType & TVC_SPECULAR) == 0)
    {
        mSurfaceSpecularColour = psProgram->resolveParameter(GpuProgramParameters::ACT_SURFACE_SPECULAR_COLOUR);
        hasError |= !(mSurfaceSpecularColour.get());    
    }


    // Get surface emissive colour if need to.
    if ((mTrackVertexColourType & TVC_EMISSIVE) == 0)
    {
        mSurfaceEmissiveColour = psProgram->resolveParameter(GpuProgramParameters::ACT_SURFACE_EMISSIVE_COLOUR);
        hasError |= !(mSurfaceEmissiveColour.get());    
    }

    // Get derived scene colour.
    mDerivedSceneColour = psProgram->resolveParameter(GpuProgramParameters::ACT_DERIVED_SCENE_COLOUR);

    // Get surface shininess.
    mSurfaceShininess = psProgram->resolveParameter(GpuProgramParameters::ACT_SURFACE_SHININESS);

    // Resolve input vertex shader normal.
    mVSInNormal = vsMain->resolveInputParameter(Parameter::SPC_NORMAL_OBJECT_SPACE);

    // Resolve output vertex shader normal.
    mVSOutNormal = vsMain->resolveOutputParameter(Parameter::SPC_NORMAL_VIEW_SPACE);

    // Resolve input pixel shader normal.
    mPSInNormal = psMain->resolveInputParameter(mVSOutNormal);

    mPSDiffuse = psMain->getInputParameter(Parameter::SPC_COLOR_DIFFUSE);
    if (mPSDiffuse.get() == NULL)
    {
        mPSDiffuse = psMain->getLocalParameter(Parameter::SPC_COLOR_DIFFUSE);
    }

    mPSOutDiffuse = psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
    mPSTempDiffuseColour = psMain->resolveLocalParameter("lPerPixelDiffuse", GCT_FLOAT4);

    hasError |= !(mDerivedSceneColour.get()) || !(mSurfaceShininess.get()) || !(mVSInNormal.get()) || !(mVSOutNormal.get()) || !(mPSInNormal.get()) || !(
        mPSDiffuse.get()) || !(mPSOutDiffuse.get()) || !(mPSTempDiffuseColour.get());
    
    if (mSpecularEnable)
    {
        mPSSpecular = psMain->getInputParameter(Parameter::SPC_COLOR_SPECULAR);
        if (mPSSpecular.get() == NULL)
        {
            mPSSpecular = psMain->resolveLocalParameter(Parameter::SPC_COLOR_SPECULAR);
        }

        mPSTempSpecularColour = psMain->resolveLocalParameter("lPerPixelSpecular", GCT_FLOAT4);

        mVSInPosition = vsMain->resolveInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);

        mVSOutViewPos = vsMain->resolveOutputParameter(Parameter::SPC_POSITION_VIEW_SPACE);

        mPSInViewPos = psMain->resolveInputParameter(mVSOutViewPos);

        mWorldViewMatrix = vsProgram->resolveParameter(GpuProgramParameters::ACT_WORLDVIEW_MATRIX);
        
        hasError |= !(mPSSpecular.get()) || !(mPSTempSpecularColour.get()) || !(mVSInPosition.get()) || !(mVSOutViewPos.get()) || 
            !(mPSInViewPos.get()) || !(mWorldViewMatrix.get());
    }

    if (hasError)
    {
        OGRE_EXCEPT( Exception::ERR_INTERNAL_ERROR, 
                "Not all parameters could be constructed for the sub-render state.",
                "PerPixelLighting::resolveGlobalParameters" );
    }
    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::resolvePerLightParameters(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain = vsProgram->getEntryPointFunction();
    Function* psMain = psProgram->getEntryPointFunction();
    bool hasError = false;

    // Resolve per light parameters.
    for (unsigned int i=0; i < mLightParamsList.size(); ++i)
    {       
        switch (mLightParamsList[i].mType)
        {
        case Light::LT_DIRECTIONAL:
            mLightParamsList[i].mDirection = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_direction_view_space");
            break;

        case Light::LT_POINT:
            mWorldViewMatrix = vsProgram->resolveParameter(GpuProgramParameters::ACT_WORLDVIEW_MATRIX);
            mVSInPosition = vsMain->resolveInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
            mLightParamsList[i].mPosition = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_position_view_space");
            mLightParamsList[i].mAttenuatParams = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_attenuation");
            
            if (mVSOutViewPos.get() == NULL)
            {
                mVSOutViewPos = vsMain->resolveOutputParameter(Parameter::SPC_POSITION_VIEW_SPACE);

                mPSInViewPos = psMain->resolveInputParameter(mVSOutViewPos);
            }   
            
            hasError |= !(mWorldViewMatrix.get()) || !(mVSInPosition.get()) || !(mLightParamsList[i].mPosition.get()) || 
                !(mLightParamsList[i].mAttenuatParams.get()) || !(mVSOutViewPos.get()) || !(mPSInViewPos.get());
        
            break;

        case Light::LT_SPOTLIGHT:
            mWorldViewMatrix = vsProgram->resolveParameter(GpuProgramParameters::ACT_WORLDVIEW_MATRIX);

            mVSInPosition = vsMain->resolveInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
            mLightParamsList[i].mPosition = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_position_view_space");
            mLightParamsList[i].mDirection = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_direction_view_space");
            mLightParamsList[i].mAttenuatParams = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_attenuation");

            mLightParamsList[i].mSpotParams = psProgram->resolveParameter(GCT_FLOAT3, -1, (uint16)GPV_LIGHTS, "spotlight_params");

            if (mVSOutViewPos.get() == NULL)
            {
                mVSOutViewPos = vsMain->resolveOutputParameter(Parameter::SPC_POSITION_VIEW_SPACE);

                mPSInViewPos = psMain->resolveInputParameter(mVSOutViewPos);
            }   

            hasError |= !(mWorldViewMatrix.get()) || !(mVSInPosition.get()) || !(mLightParamsList[i].mPosition.get()) || 
                !(mLightParamsList[i].mDirection.get()) || !(mLightParamsList[i].mAttenuatParams.get()) || 
                !(mLightParamsList[i].mSpotParams.get()) || !(mVSOutViewPos.get()) || !(mPSInViewPos.get());
            
            break;
        }

        // Resolve diffuse colour.
        if ((mTrackVertexColourType & TVC_DIFFUSE) == 0)
        {
            mLightParamsList[i].mDiffuseColour = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS | (uint16)GPV_GLOBAL, "derived_light_diffuse");
        }
        else
        {
            mLightParamsList[i].mDiffuseColour = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_diffuse");
        }   

        hasError |= !(mLightParamsList[i].mDiffuseColour.get());

        if (mSpecularEnable)
        {
            // Resolve specular colour.
            if ((mTrackVertexColourType & TVC_SPECULAR) == 0)
            {
                mLightParamsList[i].mSpecularColour = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS | (uint16)GPV_GLOBAL, "derived_light_specular");
            }
            else
            {
                mLightParamsList[i].mSpecularColour = psProgram->resolveParameter(GCT_FLOAT4, -1, (uint16)GPV_LIGHTS, "light_specular");
            }   
            hasError |= !(mLightParamsList[i].mSpecularColour.get());
        }       
    }
        
    if (hasError)
    {
        OGRE_EXCEPT( Exception::ERR_INTERNAL_ERROR, 
                "Not all parameters could be constructed for the sub-render state.",
                "PerPixelLighting::resolvePerLightParameters" );
    }
    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::resolveDependencies(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);

    vsProgram->addDependency(FFP_LIB_COMMON);
    vsProgram->addDependency(SGX_LIB_PERPIXELLIGHTING);

    psProgram->addDependency(FFP_LIB_COMMON);
    psProgram->addDependency(SGX_LIB_PERPIXELLIGHTING);

    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::addFunctionInvocations(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM); 
    Function* vsMain = vsProgram->getEntryPointFunction();  
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* psMain = psProgram->getEntryPointFunction();  

    // Add the global illumination functions.
    if (false == addVSInvocation(vsMain, FFP_VS_LIGHTING))
        return false;

    // Add the global illumination functions.
    if (false == addPSGlobalIlluminationInvocation(psMain, FFP_PS_COLOUR_BEGIN + 1))
        return false;


    // Add per light functions.
    for (unsigned int i=0; i < mLightParamsList.size(); ++i)
    {       
        if (false == addPSIlluminationInvocation(&mLightParamsList[i], psMain, FFP_PS_COLOUR_BEGIN + 1))
            return false;
    }

    // Assign back temporary variables to the ps diffuse and specular components.
    if (false == addPSFinalAssignmentInvocation(psMain, FFP_PS_COLOUR_BEGIN + 1))
        return false;


    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::addVSInvocation(Function* vsMain, const int groupOrder)
{
    auto stage = vsMain->getStage(groupOrder);

    // Transform normal in view space.
    stage.callFunction(SGX_FUNC_TRANSFORMNORMAL, mWorldViewITMatrix, mVSInNormal, mVSOutNormal);

    // Transform view space position if need to.
    if (mVSOutViewPos)
    {
        stage.callFunction(SGX_FUNC_TRANSFORMPOSITION, mWorldViewMatrix, mVSInPosition, mVSOutViewPos);
    }

    return true;
}


//-----------------------------------------------------------------------
bool PerPixelLighting::addPSGlobalIlluminationInvocation(Function* psMain, const int groupOrder)
{
    auto stage = psMain->getStage(groupOrder);

    if ((mTrackVertexColourType & TVC_AMBIENT) == 0 && 
        (mTrackVertexColourType & TVC_EMISSIVE) == 0)
    {
        stage.assign(mDerivedSceneColour, mPSTempDiffuseColour);
    }
    else
    {
        if (mTrackVertexColourType & TVC_AMBIENT)
        {
            stage.callFunction(FFP_FUNC_MODULATE, mLightAmbientColour, mPSDiffuse, mPSTempDiffuseColour);
        }
        else
        {
            stage.assign(In(mDerivedAmbientLightColour).xyz(), Out(mPSTempDiffuseColour).xyz());
        }

        if (mTrackVertexColourType & TVC_EMISSIVE)
        {
            stage.callFunction(FFP_FUNC_ADD, mPSDiffuse, mPSTempDiffuseColour, mPSTempDiffuseColour);
        }
        else
        {
            stage.callFunction(FFP_FUNC_ADD, mSurfaceEmissiveColour, mPSTempDiffuseColour, mPSTempDiffuseColour);
        }       
    }

    if (mSpecularEnable)
    {
        stage.assign(mPSSpecular, mPSTempSpecularColour);
    }
    
    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::addPSIlluminationInvocation(LightParams* curLightParams, Function* psMain, const int groupOrder)
{   
    auto stage = psMain->getStage(groupOrder);

    // Merge diffuse colour with vertex colour if need to.
    if (mTrackVertexColourType & TVC_DIFFUSE)           
    {
        stage.callFunction(FFP_FUNC_MODULATE, In(mPSDiffuse).xyz(), In(curLightParams->mDiffuseColour).xyz(),
                           Out(curLightParams->mDiffuseColour).xyz());
    }

    // Merge specular colour with vertex colour if need to.
    if (mSpecularEnable && mTrackVertexColourType & TVC_SPECULAR)
    {
        stage.callFunction(FFP_FUNC_MODULATE, In(mPSDiffuse).xyz(), In(curLightParams->mSpecularColour).xyz(),
                           Out(curLightParams->mSpecularColour).xyz());
    }

    FunctionInvocation* curFuncInvocation = NULL;   
    switch (curLightParams->mType)
    {

    case Light::LT_DIRECTIONAL:         
        if (mSpecularEnable)
        {               
            curFuncInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_LIGHT_DIRECTIONAL_DIFFUSESPECULAR, groupOrder);
            curFuncInvocation->pushOperand(mPSInNormal, Operand::OPS_IN);
            curFuncInvocation->pushOperand(mPSInViewPos, Operand::OPS_IN);          
            curFuncInvocation->pushOperand(curLightParams->mDirection, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);          
            curFuncInvocation->pushOperand(curLightParams->mSpecularColour, Operand::OPS_IN, Operand::OPM_XYZ);         
            curFuncInvocation->pushOperand(mSurfaceShininess, Operand::OPS_IN);
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);    
            curFuncInvocation->pushOperand(mPSTempSpecularColour, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_OUT, Operand::OPM_XYZ);   
            curFuncInvocation->pushOperand(mPSTempSpecularColour, Operand::OPS_OUT, Operand::OPM_XYZ);  
            psMain->addAtomInstance(curFuncInvocation);
        }

        else
        {
            curFuncInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_LIGHT_DIRECTIONAL_DIFFUSE, groupOrder);
            curFuncInvocation->pushOperand(mPSInNormal, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mDirection, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);                  
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);    
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_OUT, Operand::OPM_XYZ);   
            psMain->addAtomInstance(curFuncInvocation); 
        }   
        break;

    case Light::LT_POINT:   
        if (mSpecularEnable)
        {
            curFuncInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_LIGHT_POINT_DIFFUSESPECULAR, groupOrder);
            curFuncInvocation->pushOperand(mPSInNormal, Operand::OPS_IN);           
            curFuncInvocation->pushOperand(mPSInViewPos, Operand::OPS_IN);  
            curFuncInvocation->pushOperand(curLightParams->mPosition, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mAttenuatParams, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mSpecularColour, Operand::OPS_IN, Operand::OPM_XYZ);         
            curFuncInvocation->pushOperand(mSurfaceShininess, Operand::OPS_IN);
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);    
            curFuncInvocation->pushOperand(mPSTempSpecularColour, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_OUT, Operand::OPM_XYZ);   
            curFuncInvocation->pushOperand(mPSTempSpecularColour, Operand::OPS_OUT, Operand::OPM_XYZ);  
            psMain->addAtomInstance(curFuncInvocation);     
        }
        else
        {
            curFuncInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_LIGHT_POINT_DIFFUSE, groupOrder);
            curFuncInvocation->pushOperand(mPSInNormal, Operand::OPS_IN);           
            curFuncInvocation->pushOperand(mPSInViewPos, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mPosition, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mAttenuatParams, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);                  
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);    
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_OUT, Operand::OPM_XYZ);   
            psMain->addAtomInstance(curFuncInvocation); 
        }

        break;

    case Light::LT_SPOTLIGHT:
        if (mSpecularEnable)
        {
            curFuncInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_LIGHT_SPOT_DIFFUSESPECULAR, groupOrder);
            curFuncInvocation->pushOperand(mPSInNormal, Operand::OPS_IN);
            curFuncInvocation->pushOperand(mPSInViewPos, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mPosition, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mDirection, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mAttenuatParams, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mSpotParams, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mSpecularColour, Operand::OPS_IN, Operand::OPM_XYZ);         
            curFuncInvocation->pushOperand(mSurfaceShininess, Operand::OPS_IN);
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);    
            curFuncInvocation->pushOperand(mPSTempSpecularColour, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_OUT, Operand::OPM_XYZ);   
            curFuncInvocation->pushOperand(mPSTempSpecularColour, Operand::OPS_OUT, Operand::OPM_XYZ);  
            psMain->addAtomInstance(curFuncInvocation);         
        }
        else
        {
            curFuncInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_LIGHT_SPOT_DIFFUSE, groupOrder);
            curFuncInvocation->pushOperand(mPSInNormal, Operand::OPS_IN);
            curFuncInvocation->pushOperand(mPSInViewPos, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mPosition, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mDirection, Operand::OPS_IN, Operand::OPM_XYZ);
            curFuncInvocation->pushOperand(curLightParams->mAttenuatParams, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mSpotParams, Operand::OPS_IN);
            curFuncInvocation->pushOperand(curLightParams->mDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);                  
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_IN, Operand::OPM_XYZ);    
            curFuncInvocation->pushOperand(mPSTempDiffuseColour, Operand::OPS_OUT, Operand::OPM_XYZ);   
            psMain->addAtomInstance(curFuncInvocation); 
        }
        break;
    }

    return true;
}

//-----------------------------------------------------------------------
bool PerPixelLighting::addPSFinalAssignmentInvocation( Function* psMain, const int groupOrder)
{
    auto stage = psMain->getStage(groupOrder);
    stage.assign(mPSTempDiffuseColour, mPSDiffuse);
    stage.assign(mPSDiffuse, mPSOutDiffuse);

    if (mSpecularEnable)
    {
        stage.assign(mPSTempSpecularColour, mPSSpecular);
    }

    return true;
}


//-----------------------------------------------------------------------
void PerPixelLighting::copyFrom(const SubRenderState& rhs)
{
    const PerPixelLighting& rhsLighting = static_cast<const PerPixelLighting&>(rhs);

    int lightCount[3];

    rhsLighting.getLightCount(lightCount);
    setLightCount(lightCount);
}

//-----------------------------------------------------------------------
bool PerPixelLighting::preAddToRenderState(const RenderState* renderState, Pass* srcPass, Pass* dstPass)
{
    if (srcPass->getLightingEnabled() == false)
        return false;

    int lightCount[3];

    renderState->getLightCount(lightCount);

    setTrackVertexColourType(srcPass->getVertexColourTracking());           

    if (srcPass->getShininess() > 0.0 &&
        srcPass->getSpecular() != ColourValue::Black)
    {
        setSpecularEnable(true);
    }
    else
    {
        setSpecularEnable(false);   
    }

    // Case this pass should run once per light(s) -> override the light policy.
    if (srcPass->getIteratePerLight())
    {       

        // This is the preferred case -> only one type of light is handled.
        if (srcPass->getRunOnlyForOneLightType())
        {
            if (srcPass->getOnlyLightType() == Light::LT_POINT)
            {
                lightCount[0] = srcPass->getLightCountPerIteration();
                lightCount[1] = 0;
                lightCount[2] = 0;
            }
            else if (srcPass->getOnlyLightType() == Light::LT_DIRECTIONAL)
            {
                lightCount[0] = 0;
                lightCount[1] = srcPass->getLightCountPerIteration();
                lightCount[2] = 0;
            }
            else if (srcPass->getOnlyLightType() == Light::LT_SPOTLIGHT)
            {
                lightCount[0] = 0;
                lightCount[1] = 0;
                lightCount[2] = srcPass->getLightCountPerIteration();
            }
        }

        // This is worse case -> all light types expected to be handled.
        // Can not handle this request in efficient way - throw an exception.
        else
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                "Using iterative lighting method with RT Shader System requires specifying explicit light type.",
                "PerPixelLighting::preAddToRenderState");           
        }
    }

    setLightCount(lightCount);

    return true;
}

//-----------------------------------------------------------------------
void PerPixelLighting::setLightCount(const int lightCount[3])
{
    for (int type=0; type < 3; ++type)
    {
        for (int i=0; i < lightCount[type]; ++i)
        {
            LightParams curParams;

            if (type == 0)
                curParams.mType = Light::LT_POINT;
            else if (type == 1)
                curParams.mType = Light::LT_DIRECTIONAL;
            else if (type == 2)
                curParams.mType = Light::LT_SPOTLIGHT;      

            mLightParamsList.push_back(curParams);
        }
    }           
}

//-----------------------------------------------------------------------
void PerPixelLighting::getLightCount(int lightCount[3]) const
{
    lightCount[0] = 0;
    lightCount[1] = 0;
    lightCount[2] = 0;

    for (unsigned int i=0; i < mLightParamsList.size(); ++i)
    {
        const LightParams curParams = mLightParamsList[i];

        if (curParams.mType == Light::LT_POINT)
            lightCount[0]++;
        else if (curParams.mType == Light::LT_DIRECTIONAL)
            lightCount[1]++;
        else if (curParams.mType == Light::LT_SPOTLIGHT)
            lightCount[2]++;
    }
}


//-----------------------------------------------------------------------
const String& PerPixelLightingFactory::getType() const
{
    return PerPixelLighting::Type;
}

//-----------------------------------------------------------------------
SubRenderState* PerPixelLightingFactory::createInstance(ScriptCompiler* compiler, 
                                                        PropertyAbstractNode* prop, Pass* pass, SGScriptTranslator* translator)
{
    if (prop->name == "lighting_stage")
    {
        if(prop->values.size() == 1)
        {
            String modelType;
            
            if(false == SGScriptTranslator::getString(prop->values.front(), &modelType))
            {
                compiler->addError(ScriptCompiler::CE_INVALIDPARAMETERS, prop->file, prop->line);
                return NULL;
            }
            
            if (modelType == "per_pixel")
            {
                return createOrRetrieveInstance(translator);
            }
        }       
    }

    return NULL;
}

//-----------------------------------------------------------------------
void PerPixelLightingFactory::writeInstance(MaterialSerializer* ser, SubRenderState* subRenderState, 
                                            Pass* srcPass, Pass* dstPass)
{
    ser->writeAttribute(4, "lighting_stage");
    ser->writeValue("per_pixel");
}

//-----------------------------------------------------------------------
SubRenderState* PerPixelLightingFactory::createInstanceImpl()
{
    return OGRE_NEW PerPixelLighting;
}

}
}

#endif
