/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

// setting that here because otherwise SampleFormat is defined to AVSampleFormat
// which we don't use here
#define FF_API_OLD_SAMPLE_FMT 0
#define DEFAULT_STREAM_INDEX (0)

#include "DXVAHD.h"

#include "VideoRenderers/RenderFlags.h"
#include "VideoRenderers/RenderManager.h"
#include "VideoRenderers/windows/RendererBase.h"
#include "VideoRenderers/windows/RendererDXVA.h"
#include "rendering/dx/RenderContext.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <mutex>

#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_5.h>

using namespace DXVA;
using namespace Microsoft::WRL;

namespace
{
// magic constants taken from Chromium:
// https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/gl/swap_chain_presenter.cc#180
constexpr GUID GUID_INTEL_VPE_INTERFACE = {
    0xedd1d4b9, 0x8659, 0x4cbc, {0xa4, 0xd6, 0x98, 0x31, 0xa2, 0x16, 0x3a, 0xc3}};

constexpr UINT kIntelVpeFnVersion = 0x01;
constexpr UINT kIntelVpeFnMode = 0x20;
constexpr UINT kIntelVpeFnScaling = 0x37;
constexpr UINT kIntelVpeVersion3 = 0x0003;
constexpr UINT kIntelVpeModePreproc = 0x01;
constexpr UINT kIntelVpeScalingSuperResolution = 0x2;

constexpr GUID GUID_NVIDIA_PPE_INTERFACE = {
    0xd43ce1b3, 0x1f4b, 0x48ac, {0xba, 0xee, 0xc3, 0xc2, 0x53, 0x75, 0xe6, 0xf7}};

constexpr UINT kStreamExtensionVersionV1 = 0x1;
constexpr UINT kStreamExtensionMethodSuperResolution = 0x2;
} // unnamed namespace

struct DXVA::ProcColorSpaces
{
  DXGI_COLOR_SPACE_TYPE inputColorSpace;
  DXGI_COLOR_SPACE_TYPE outputColorSpace;
};

CProcessorHD::CProcessorHD()
{
  DX::Windowing()->Register(this);
}

CProcessorHD::~CProcessorHD()
{
  DX::Windowing()->Unregister(this);
  UnInit();
}

void CProcessorHD::UnInit()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  Close();
}

void CProcessorHD::Close()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  m_enumerator = nullptr;
  m_pVideoProcessor = nullptr;
  m_pVideoContext = nullptr;
  m_pVideoDevice = nullptr;
  m_superResolutionEnabled = false;
}

bool CProcessorHD::PreInit() const
{
  ComPtr<ID3D11VideoDevice> pVideoDevice;
  ComPtr<ID3D11VideoProcessorEnumerator> pEnumerator;
  ComPtr<ID3D11Device> pD3DDevice = DX::DeviceResources::Get()->GetD3DDevice();

  if (FAILED(pD3DDevice.As(&pVideoDevice)))
  {
    CLog::LogF(LOGWARNING, "failed to get video device.");
    return false;
  }

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc1 = {};
  desc1.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
  desc1.InputWidth = 640;
  desc1.InputHeight = 480;
  desc1.OutputWidth = 640;
  desc1.OutputHeight = 480;
  desc1.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  // try to create video enum
  if (FAILED(pVideoDevice->CreateVideoProcessorEnumerator(&desc1, &pEnumerator)))
  {
    CLog::LogF(LOGWARNING, "failed to create Video Enumerator.");
    return false;
  }
  return true;
}

bool CProcessorHD::InitProcessor()
{
  HRESULT hr{};
  m_pVideoDevice = nullptr;
  m_pVideoContext = nullptr;

  m_enumerator = std::make_unique<CEnumeratorHD>();
  if (!m_enumerator->Open(m_width, m_height, m_input_dxgi_format))
    return false;

  ComPtr<ID3D11DeviceContext1> pD3DDeviceContext = DX::DeviceResources::Get()->GetImmediateContext();
  ComPtr<ID3D11Device> pD3DDevice = DX::DeviceResources::Get()->GetD3DDevice();

  if (FAILED(hr = pD3DDeviceContext.As(&m_pVideoContext)))
  {
    CLog::LogF(LOGWARNING, "video context initialization is failed. Error {}",
               DX::GetErrorDescription(hr));
    return false;
  }
  if (FAILED(hr = pD3DDevice.As(&m_pVideoDevice)))
  {
    CLog::LogF(LOGWARNING, "video device initialization is failed. Error {}",
               DX::GetErrorDescription(hr));
    return false;
  }

  m_procCaps = m_enumerator->ProbeProcessorCaps();
  if (!m_procCaps.m_valid)
    return false;

  return true;
}

bool CProcessorHD::CheckFormats() const
{
  // check default output format (as render target)
  return m_enumerator && m_enumerator->IsFormatSupportedOutput(m_output_dxgi_format);
}

bool CProcessorHD::IsFormatConversionSupported(DXGI_FORMAT inputFormat,
                                               DXGI_FORMAT outputFormat,
                                               const VideoPicture& picture)
{
  std::unique_lock<CCriticalSection> lock(m_section);

  // accept the conversion unless the API can be called successfully and disallows it
  BOOL supported{TRUE};

  if (!m_enumerator || !m_enumerator->IsEnumerator1Available())
    return true;

  ProcColorSpaces spaces = CalculateDXGIColorSpaces(DXGIColorSpaceArgs(picture));

  if (m_enumerator->CheckConversion(inputFormat, spaces.inputColorSpace, outputFormat,
                                    spaces.outputColorSpace))
  {
    CLog::LogF(
        LOGDEBUG, "conversion from {} / {} to {} / {} is {}supported.",
        DX::DXGIFormatToString(inputFormat), DX::DXGIColorSpaceTypeToString(spaces.inputColorSpace),
        DX::DXGIFormatToString(outputFormat),
        DX::DXGIColorSpaceTypeToString(spaces.outputColorSpace), supported == TRUE ? "" : "NOT ");
    return true;
  }

  return false;
}

bool CProcessorHD::Open(const VideoPicture& picture)
{
  Close();

  std::unique_lock<CCriticalSection> lock(m_section);

  m_width = picture.iWidth;
  m_height = picture.iHeight;
  m_color_primaries = static_cast<AVColorPrimaries>(picture.color_primaries);
  m_color_transfer = static_cast<AVColorTransferCharacteristic>(picture.color_transfer);
  const AVPixelFormat av_pixel_format = picture.videoBuffer->GetFormat();
  m_input_dxgi_format =
      CRendererDXVA::GetDXGIFormat(av_pixel_format, CRendererBase::GetDXGIFormat(picture));

  if (!InitProcessor())
    return false;

  return OpenProcessor();
}

bool CProcessorHD::ReInit()
{
  std::unique_lock<CCriticalSection> lock(m_section);
  Close();

  if (!InitProcessor())
    return false;

  if (!CheckFormats())
    return false;

  return true;
}

bool CProcessorHD::OpenProcessor()
{
  std::unique_lock<CCriticalSection> lock(m_section);

  // restore the device if it was lost
  if ((!m_enumerator || !m_enumerator->IsInitialized()) && !ReInit())
    return false;

  CLog::LogF(LOGDEBUG, "creating processor.");

  // create processor
  m_pVideoProcessor = m_enumerator->CreateVideoProcessor(m_procCaps.m_procIndex);

  if (!m_pVideoProcessor)
  {
    CLog::LogF(LOGDEBUG, "failed creating video processor.");
    return false;
  }

  // Output background color (black)
  D3D11_VIDEO_COLOR color;
  color.YCbCr = { 0.0625f, 0.5f, 0.5f, 1.0f }; // black color
  m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor.Get(), TRUE, &color);

  // AMD/HDR (as of 2023-06-16): processor tone maps by default and modifies high code values
  // We want "passthrough" of the signal and to do our own tone mapping when needed.
  // Disable the functionality by pretending that the display supports all PQ levels (0-10000)
  DXGI_ADAPTER_DESC ad{};
  DX::DeviceResources::Get()->GetAdapterDesc(&ad);
  bool streamIsHDR =
      (m_color_primaries == AVCOL_PRI_BT2020) &&
      (m_color_transfer == AVCOL_TRC_SMPTE2084 || m_color_transfer == AVCOL_TRC_ARIB_STD_B67);

  if (m_procCaps.m_hasMetadataHDR10Support && ad.VendorId == PCIV_AMD && streamIsHDR)
  {
    ComPtr<ID3D11VideoContext2> videoCtx2;
    if (SUCCEEDED(m_pVideoContext.As(&videoCtx2)))
    {
      DXGI_HDR_METADATA_HDR10 hdr10{};
      hdr10.MaxMasteringLuminance = 10000;
      hdr10.MinMasteringLuminance = 0;

      videoCtx2->VideoProcessorSetOutputHDRMetaData(m_pVideoProcessor.Get(),
                                                    DXGI_HDR_METADATA_TYPE_HDR10,
                                                    sizeof(DXGI_HDR_METADATA_HDR10), &hdr10);

      CLog::LogF(LOGDEBUG, "video processor tone mapping disabled.");
    }
    else
    {
      CLog::LogF(LOGDEBUG,
                 "unable to retrieve ID3D11VideoContext2 to disable video processor tone mapping.");
    }
  }

  return true;
}

void CProcessorHD::ApplyFilter(D3D11_VIDEO_PROCESSOR_FILTER filter, int value, int min, int max, int def) const
{
  if (filter >= static_cast<D3D11_VIDEO_PROCESSOR_FILTER>(NUM_FILTERS))
    return;

  // Unsupported filter. Ignore.
  if (!m_procCaps.m_Filters[filter].bSupported)
    return;

  D3D11_VIDEO_PROCESSOR_FILTER_RANGE range = m_procCaps.m_Filters[filter].Range;
  int val;

  if(value > def)
    val = range.Default + (range.Maximum - range.Default) * (value - def) / (max - def);
  else if(value < def)
    val = range.Default + (range.Minimum - range.Default) * (value - def) / (min - def);
  else
    val = range.Default;

  m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX, filter, val != range.Default, val);
}

ComPtr<ID3D11VideoProcessorInputView> CProcessorHD::GetInputView(CRenderBuffer* view) const
{
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC vpivd = {0, D3D11_VPIV_DIMENSION_TEXTURE2D, {0, 0}};

  ComPtr<ID3D11Resource> resource;
  unsigned arrayIdx = 0;
  HRESULT hr = view->GetResource(resource.GetAddressOf(), &arrayIdx);
  if (SUCCEEDED(hr))
  {
    vpivd.Texture2D.ArraySlice = arrayIdx;
    return m_enumerator->CreateVideoProcessorInputView(resource.Get(), &vpivd);
  }
  return nullptr;
}

DXGI_COLOR_SPACE_TYPE CProcessorHD::GetDXGIColorSpaceSource(const DXGIColorSpaceArgs& csArgs,
                                                            bool supportHDR,
                                                            bool supportHLG,
                                                            bool BT2020Left,
                                                            bool HDRLeft)
{
  // RGB
  if (csArgs.color_space == AVCOL_SPC_RGB)
  {
    if (!csArgs.full_range)
    {
      if (csArgs.primaries == AVCOL_PRI_BT2020)
      {
        if (csArgs.color_transfer == AVCOL_TRC_SMPTEST2084 && supportHDR)
          return DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;

        return DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
      }
      return DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
    }

    if (csArgs.primaries == AVCOL_PRI_BT2020)
    {
      if (csArgs.color_transfer == AVCOL_TRC_SMPTEST2084)
        return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

      return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
    }
    if (csArgs.color_transfer == AVCOL_TRC_LINEAR || csArgs.color_transfer == AVCOL_TRC_LOG)
      return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  }
  // UHDTV
  if (csArgs.primaries == AVCOL_PRI_BT2020)
  {
    // Windows 10 doesn't support HLG passthrough, always is used PQ for HDR passthrough
    if ((csArgs.color_transfer == AVCOL_TRC_SMPTEST2084 ||
         csArgs.color_transfer == AVCOL_TRC_ARIB_STD_B67) &&
        supportHDR) // is HDR display ON
      return (HDRLeft) ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020
                       : DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020;

    // HLG transfer can be used for HLG source in SDR display if is supported
    if (csArgs.color_transfer == AVCOL_TRC_ARIB_STD_B67 && supportHLG) // driver supports HLG
      return DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;

    if (csArgs.full_range)
      return DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020;

    return (BT2020Left) ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020
                        : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020;
  }
  // SDTV
  if (csArgs.primaries == AVCOL_PRI_BT470BG || csArgs.primaries == AVCOL_PRI_SMPTE170M)
  {
    if (csArgs.full_range)
      return DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;

    return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
  }
  // HDTV
  if (csArgs.full_range)
  {
    if (csArgs.color_transfer == AVCOL_TRC_SMPTE170M)
      return DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601;

    return DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709;
  }

  return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
}

DXGI_COLOR_SPACE_TYPE CProcessorHD::GetDXGIColorSpaceTarget(const DXGIColorSpaceArgs& csArgs,
                                                            bool supportHDR,
                                                            bool limitedRange)
{
  DXGI_COLOR_SPACE_TYPE color = limitedRange ? DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709
                                             : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

  if (!supportHDR)
    return color;

  // HDR10 or HLG
  if (csArgs.primaries == AVCOL_PRI_BT2020 && (csArgs.color_transfer == AVCOL_TRC_SMPTE2084 ||
                                               csArgs.color_transfer == AVCOL_TRC_ARIB_STD_B67))
  {
    color = limitedRange ? DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020
                         : DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  }

  return color;
}

bool CProcessorHD::Render(CRect src, CRect dst, ID3D11Resource* target, CRenderBuffer** views, DWORD flags, UINT frameIdx, UINT rotation, float contrast, float brightness)
{
  std::unique_lock<CCriticalSection> lock(m_section);

  // restore processor if it was lost
  if (!m_pVideoProcessor && !OpenProcessor())
    return false;

  if (!views[2])
    return false;

  RECT sourceRECT = {static_cast<LONG>(src.x1), static_cast<LONG>(src.y1),
                     static_cast<LONG>(src.x2), static_cast<LONG>(src.y2)};
  RECT dstRECT = {static_cast<LONG>(dst.x1), static_cast<LONG>(dst.y1), static_cast<LONG>(dst.x2),
                  static_cast<LONG>(dst.y2)};

  D3D11_VIDEO_FRAME_FORMAT dxvaFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

  unsigned int providedPast = 0;
  for (int i = 3; i < 8; i++)
  {
    if (views[i])
      providedPast++;
  }
  unsigned int providedFuture = 0;
  for (int i = 1; i >= 0; i--)
  {
    if (views[i])
      providedFuture++;
  }
  const int futureFrames = std::min(providedFuture, m_procCaps.m_rateCaps.FutureFrames);
  const int pastFrames = std::min(providedPast, m_procCaps.m_rateCaps.PastFrames);
  std::vector<ID3D11VideoProcessorInputView*> pastViews(pastFrames, nullptr);
  std::vector<ID3D11VideoProcessorInputView*> futureViews(futureFrames, nullptr);

  D3D11_VIDEO_PROCESSOR_STREAM stream_data = {};
  stream_data.Enable = TRUE;
  stream_data.PastFrames = pastFrames;
  stream_data.FutureFrames = futureFrames;
  stream_data.ppPastSurfaces = pastViews.data();
  stream_data.ppFutureSurfaces = futureViews.data();

  std::vector<ComPtr<ID3D11VideoProcessorInputView>> all_views;
  const int start = 2 - futureFrames;
  const int end = 2 + pastFrames;
  int count = 0;

  for (int i = start; i <= end; i++)
  {
    if (!views[i])
      continue;

    ComPtr<ID3D11VideoProcessorInputView> view = GetInputView(views[i]);

    if (i > 2)
    {
      // frames order should be { ?, T-3, T-2, T-1 }
      pastViews[2 + pastFrames - i] = view.Get();
    }
    else if (i == 2)
    {
      stream_data.pInputSurface = view.Get();
    }
    else if (i < 2)
    {
      // frames order should be { T+1, T+2, T+3, .. }
      futureViews[1 - i] = view.Get();
    }
    if (view)
    {
      count++;
      all_views.push_back(view);
    }
  }

  if (count != pastFrames + futureFrames + 1)
  {
    CLog::LogF(LOGERROR, "incomplete views set.");
    return false;
  }

  if (flags & RENDER_FLAG_FIELD0 && flags & RENDER_FLAG_TOP)
    dxvaFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
  else if (flags & RENDER_FLAG_FIELD1 && flags & RENDER_FLAG_BOT)
    dxvaFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
  if (flags & RENDER_FLAG_FIELD0 && flags & RENDER_FLAG_BOT)
    dxvaFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST;
  if (flags & RENDER_FLAG_FIELD1 && flags & RENDER_FLAG_TOP)
    dxvaFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST;

  bool frameProgressive = dxvaFrameFormat == D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

  // Progressive or Interlaced video at normal rate.
  stream_data.InputFrameOrField = frameIdx;
  stream_data.OutputIndex = flags & RENDER_FLAG_FIELD1 && !frameProgressive ? 1 : 0;

  // input format
  m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX, dxvaFrameFormat);
  // Source rect
  m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX, TRUE, &sourceRECT);
  // Stream dest rect
  m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX, TRUE, &dstRECT);
  // Output rect
  m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor.Get(), TRUE, &dstRECT);

  ComPtr<ID3D11VideoContext1> videoCtx1;
  if (SUCCEEDED(m_pVideoContext.As(&videoCtx1)))
  {
    ProcColorSpaces spaces = CalculateDXGIColorSpaces(DXGIColorSpaceArgs(*views[2]));

    videoCtx1->VideoProcessorSetStreamColorSpace1(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX,
                                                  spaces.inputColorSpace);
    videoCtx1->VideoProcessorSetOutputColorSpace1(m_pVideoProcessor.Get(), spaces.outputColorSpace);
    // makes target available for processing in shaders
    videoCtx1->VideoProcessorSetOutputShaderUsage(m_pVideoProcessor.Get(), 1);
  }
  else
  {
    // input colorspace
    bool isBT601 = views[2]->color_space == AVCOL_SPC_BT470BG || views[2]->color_space == AVCOL_SPC_SMPTE170M;
    // clang-format off
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace
    {
      0u,                             // 0 - Playback, 1 - Processing
      views[2]->full_range ? 0u : 1u, // 0 - Full (0-255), 1 - Limited (16-235) (RGB)
      isBT601 ? 1u : 0u,              // 0 - BT.601, 1 - BT.709
      0u,                             // 0 - Conventional YCbCr, 1 - xvYCC
      views[2]->full_range ? 2u : 1u  // 0 - driver defaults, 2 - Full range [0-255], 1 - Studio range [16-235] (YUV)
    };
    // clang-format on
    m_pVideoContext->VideoProcessorSetStreamColorSpace(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX, &colorSpace);
    // Output color space
    // don't apply any color range conversion, this will be fixed at later stage.
    colorSpace.Usage = 0;  // 0 - playback, 1 - video processing
    colorSpace.RGB_Range = DX::Windowing()->UseLimitedColor() ? 1 : 0;  // 0 - 0-255, 1 - 16-235
    colorSpace.YCbCr_Matrix = 1;  // 0 - BT.601, 1 = BT.709
    colorSpace.YCbCr_xvYCC = 1;  // 0 - Conventional YCbCr, 1 - xvYCC
    colorSpace.Nominal_Range = 0;  // 2 - 0-255, 1 = 16-235, 0 - undefined
    m_pVideoContext->VideoProcessorSetOutputColorSpace(m_pVideoProcessor.Get(), &colorSpace);
  }

  // brightness
  ApplyFilter(D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, static_cast<int>(brightness), 0, 100, 50);
  // contrast
  ApplyFilter(D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST, static_cast<int>(contrast), 0, 100, 50);
  // unused filters
  ApplyFilter(D3D11_VIDEO_PROCESSOR_FILTER_HUE, 50, 0, 100, 50);
  ApplyFilter(D3D11_VIDEO_PROCESSOR_FILTER_SATURATION, 50, 0, 100, 50);
  // Rotation
  m_pVideoContext->VideoProcessorSetStreamRotation(m_pVideoProcessor.Get(), DEFAULT_STREAM_INDEX, rotation != 0,
                                                   static_cast<D3D11_VIDEO_PROCESSOR_ROTATION>(rotation / 90));

  // create output view for surface.
  const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {D3D11_VPOV_DIMENSION_TEXTURE2D,
                                                                 {0}};
  ComPtr<ID3D11VideoProcessorOutputView> pOutputView =
      m_enumerator->CreateVideoProcessorOutputView(target, &outputViewDesc);

  HRESULT hr{};
  if (pOutputView)
  {
    hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor.Get(), pOutputView.Get(), frameIdx, 1, &stream_data);
    if (S_OK != hr)
    {
      CLog::LogF(FAILED(hr) ? LOGERROR : LOGWARNING,
                 "VideoProcessorBlt returned {} while VideoProcessorBlt execution.",
                 DX::GetErrorDescription(hr));
    }
  }

  return pOutputView && !FAILED(hr);
}

ProcColorSpaces CProcessorHD::CalculateDXGIColorSpaces(const DXGIColorSpaceArgs& csArgs) const
{
  const bool supportHDR = DX::Windowing()->IsHDROutput();

  return ProcColorSpaces{
      GetDXGIColorSpaceSource(csArgs, supportHDR, m_procCaps.m_bSupportHLG, m_procCaps.m_BT2020Left,
                              m_procCaps.m_HDR10Left),
      GetDXGIColorSpaceTarget(csArgs, supportHDR, DX::Windowing()->UseLimitedColor())};
}

void CProcessorHD::LogSupportedConversions(const DXGI_FORMAT& inputFormat,
                                           const DXGI_FORMAT& heuristicsOutputFormat,
                                           const VideoPicture& picture)
{
  std::unique_lock<CCriticalSection> lock(m_section);

  // Defaults used by Kodi
  const DXGIColorSpaceArgs csArgs = DXGIColorSpaceArgs(picture);
  const ProcColorSpaces heuristicsCS = CalculateDXGIColorSpaces(csArgs);
  const DXGI_COLOR_SPACE_TYPE inputNativeCS = AvToDxgiColorSpace(csArgs);

  m_enumerator->LogSupportedConversions(inputFormat, heuristicsCS.inputColorSpace, inputNativeCS,
                                        heuristicsOutputFormat, heuristicsCS.outputColorSpace);
}

DXGI_COLOR_SPACE_TYPE CProcessorHD::AvToDxgiColorSpace(const DXGIColorSpaceArgs& csArgs)
{
  // RGB
  if (csArgs.color_space == AVCOL_SPC_RGB)
  {
    if (!csArgs.full_range)
    {
      if (csArgs.primaries == AVCOL_PRI_BT2020)
      {
        if (csArgs.color_transfer == AVCOL_TRC_SMPTEST2084)
          return DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;

        return DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
      }
      return DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
    }

    if (csArgs.primaries == AVCOL_PRI_BT2020)
    {
      if (csArgs.color_transfer == AVCOL_TRC_SMPTEST2084)
        return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

      return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
    }
    if (csArgs.color_transfer == AVCOL_TRC_LINEAR || csArgs.color_transfer == AVCOL_TRC_LOG)
      return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  }
  // UHDTV
  if (csArgs.primaries == AVCOL_PRI_BT2020)
  {
    if (csArgs.color_transfer == AVCOL_TRC_SMPTEST2084)
      // Full range DXGI_COLOR_SPACE_YCBCR_FULL_G2084_TOPLEFT_P2020 does not exist at this time
      // Chroma siting top left is always preferred for 4K HDR10 because is used in UHD Blu-Ray's
      // Also not considering for now FFmpeg flags because can be missing or bad flagged anyway
      return DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020;

    // HLG transfer can be used for HLG source in SDR display if is supported
    if (csArgs.color_transfer == AVCOL_TRC_ARIB_STD_B67)
    {
      if (csArgs.full_range)
        return DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020;
      else
        return DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;
    }

    if (csArgs.full_range)
      return DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020;
    else
      return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020;
  }
  // SDTV
  if (csArgs.primaries == AVCOL_PRI_BT470BG || csArgs.primaries == AVCOL_PRI_SMPTE170M)
  {
    if (csArgs.full_range)
      return DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;

    return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
  }
  // HDTV
  if (csArgs.full_range)
  {
    if (csArgs.color_transfer == AVCOL_TRC_SMPTE170M)
      return DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601;

    return DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709;
  }

  return DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
}

bool CProcessorHD::IsSuperResolutionSuitable(const VideoPicture& picture)
{
  if (picture.iWidth > 1920)
    return false;

  const UINT outputWidth = DX::Windowing()->GetBackBuffer().GetWidth();

  if (outputWidth <= picture.iWidth)
    return false;

  if (picture.iFlags & DVP_FLAG_INTERLACED)
    return false;

  if (picture.color_primaries == AVCOL_PRI_BT2020 ||
      picture.color_transfer == AVCOL_TRC_SMPTE2084 ||
      picture.color_transfer == AVCOL_TRC_ARIB_STD_B67)
    return false;

  return true;
}

void CProcessorHD::TryEnableVideoSuperResolution()
{
  if (!m_pVideoContext || !m_pVideoProcessor)
    return;

  DXGI_ADAPTER_DESC ad{};
  DX::DeviceResources::Get()->GetAdapterDesc(&ad);

  if (ad.VendorId == PCIV_Intel)
  {
    EnableIntelVideoSuperResolution();
  }
  else if (ad.VendorId == PCIV_NVIDIA)
  {
    EnableNvidiaRTXVideoSuperResolution();
  }
}

void CProcessorHD::EnableIntelVideoSuperResolution()
{
  UINT param = 0;

  struct IntelVpeExt
  {
    UINT function;
    void* param;
  };

  IntelVpeExt ext{0, &param};

  ext.function = kIntelVpeFnVersion;
  param = kIntelVpeVersion3;

  HRESULT hr = m_pVideoContext->VideoProcessorSetOutputExtension(
      m_pVideoProcessor.Get(), &GUID_INTEL_VPE_INTERFACE, sizeof(ext), &ext);
  if (FAILED(hr))
  {
    CLog::LogF(LOGWARNING, "Failed to set the Intel VPE version with error {}.",
               DX::GetErrorDescription(hr));
    return;
  }

  ext.function = kIntelVpeFnMode;
  param = kIntelVpeModePreproc;

  hr = m_pVideoContext->VideoProcessorSetOutputExtension(
      m_pVideoProcessor.Get(), &GUID_INTEL_VPE_INTERFACE, sizeof(ext), &ext);
  if (FAILED(hr))
  {
    CLog::LogF(LOGWARNING, "Failed to set the Intel VPE mode with error {}.",
               DX::GetErrorDescription(hr));
    return;
  }

  ext.function = kIntelVpeFnScaling;
  param = kIntelVpeScalingSuperResolution;

  hr = m_pVideoContext->VideoProcessorSetStreamExtension(
      m_pVideoProcessor.Get(), 0, &GUID_INTEL_VPE_INTERFACE, sizeof(ext), &ext);
  if (FAILED(hr))
  {
    CLog::LogF(LOGWARNING, "Failed to set the Intel VPE scaling type with error {}.",
               DX::GetErrorDescription(hr));
    return;
  }

  CLog::LogF(LOGINFO, "Intel Video Super Resolution request enable successfully");
  m_superResolutionEnabled = true;
}

void CProcessorHD::EnableNvidiaRTXVideoSuperResolution()
{
  struct NvidiaStreamExt
  {
    UINT version;
    UINT method;
    UINT enable;
  };

  NvidiaStreamExt ext = {kStreamExtensionVersionV1, kStreamExtensionMethodSuperResolution, 1u};

  HRESULT hr = m_pVideoContext->VideoProcessorSetStreamExtension(
      m_pVideoProcessor.Get(), 0, &GUID_NVIDIA_PPE_INTERFACE, sizeof(ext), &ext);
  if (FAILED(hr))
  {
    CLog::LogF(LOGWARNING, "Failed to set the NVIDIA video process stream extension with error {}.",
               DX::GetErrorDescription(hr));
    return;
  }

  CLog::LogF(LOGINFO, "RTX Video Super Resolution request enable successfully");
  m_superResolutionEnabled = true;
}

bool CProcessorHD::SetOutputFormat(DXGI_FORMAT outputFormat)
{
  std::unique_lock<CCriticalSection> lock(m_section);

  if (m_enumerator && m_enumerator->IsFormatSupportedOutput(outputFormat))
  {
    m_output_dxgi_format = outputFormat;
    return true;
  }
  return false;
}

bool CProcessorHD::IsFormatSupportedInput(DXGI_FORMAT format)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  return m_enumerator && m_enumerator->IsFormatSupportedInput(format);
}

bool CProcessorHD::IsFormatSupportedOutput(DXGI_FORMAT format)
{
  std::unique_lock<CCriticalSection> lock(m_section);
  return m_enumerator && m_enumerator->IsFormatSupportedOutput(format);
}
