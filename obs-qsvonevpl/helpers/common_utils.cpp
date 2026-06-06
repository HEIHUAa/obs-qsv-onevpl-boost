#include "common_utils.hpp"
#include "../obs-qsv-onevpl-encoder.hpp"

struct adapter_info AdaptersInfo[MAX_ADAPTERS] = {};
size_t AdaptersCount = 0;

// Encoder data registry
std::map<obs_encoder_t *, plugin_context *> EncoderDataMap;
std::mutex EncoderDataMapMutex;

// Pending ROI config for encoder types not yet instantiated
std::map<std::string, pending_roi_config> PendingROIConfig;
std::mutex PendingROIMutex;

// Single global ROI config (used by the ROI Editor dialog)
pending_roi_config GlobalROIConfig;
std::mutex GlobalROIConfigMutex;

void RegisterEncoderData(obs_encoder_t *Encoder, plugin_context *Context) {
  {
    std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
    EncoderDataMap[Encoder] = Context;
  }

  // Check if there is a pending ROI config for this encoder type
  const char *enc_id = obs_encoder_get_id(Encoder);
  if (!enc_id)
    return;

  std::lock_guard<std::mutex> lock(PendingROIMutex);
  auto it = PendingROIConfig.find(enc_id);
  if (it != PendingROIConfig.end()) {
    UpdateEncoderROI(Context, it->second.Regions, it->second.Mode,
                     it->second.Enabled);
    PendingROIConfig.erase(it);
    blog(LOG_INFO,
         "[QSV VPL] Applied pending ROI config for encoder type: %s",
         enc_id);
  }

  // (2) Global ROI config — always applied, never consumed
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    if (GlobalROIConfig.Enabled && !GlobalROIConfig.Regions.empty()) {
      UpdateEncoderROI(Context, GlobalROIConfig.Regions,
                       GlobalROIConfig.Mode, GlobalROIConfig.Enabled);
      blog(LOG_INFO,
           "[QSV VPL] Applied global ROI config to encoder: %s",
           enc_id);
    }
  }
}

void UnregisterEncoderData(obs_encoder_t *Encoder) {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  EncoderDataMap.erase(Encoder);
}

plugin_context *LookupEncoderData(obs_encoder_t *Encoder) {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  auto it = EncoderDataMap.find(Encoder);
  return (it != EncoderDataMap.end()) ? it->second : nullptr;
}

