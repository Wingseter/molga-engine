#include "Core/Importers/AudioImporter.h"
#include "miniaudio.h"

#include <cmath>
#include <filesystem>

namespace molga {

ImportResult AudioImporter::Import(const std::string& absSourcePath) const {
    return Import(absSourcePath, {{"loadMode", "DecodeOnLoad"}});
}

ImportResult AudioImporter::Import(const std::string& absSourcePath,
                                   const nlohmann::json& settings) const {
    ImportResult r;
    if (!std::filesystem::is_regular_file(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }

    const std::string loadMode = settings.is_object()
        ? settings.value("loadMode", std::string("DecodeOnLoad"))
        : std::string("DecodeOnLoad");
    if (loadMode != "DecodeOnLoad" && loadMode != "Streaming") {
        r.error = "invalid audio loadMode '" + loadMode + "': " + absSourcePath;
        return r;
    }

    ma_decoder decoder{};
    const ma_decoder_config decoderConfig = ma_decoder_config_init_default();
    if (ma_decoder_init_file(absSourcePath.c_str(), &decoderConfig, &decoder) != MA_SUCCESS) {
        r.error = "audio decoder rejected file: " + absSourcePath;
        return r;
    }

    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    ma_uint64 lengthInFrames = 0;
    const ma_result formatResult = ma_decoder_get_data_format(
        &decoder, &format, &channels, &sampleRate, nullptr, 0);
    const ma_result lengthResult =
        ma_decoder_get_length_in_pcm_frames(&decoder, &lengthInFrames);
    ma_decoder_uninit(&decoder);

    if (formatResult != MA_SUCCESS || lengthResult != MA_SUCCESS ||
        format == ma_format_unknown || channels == 0 || sampleRate == 0) {
        r.error = "audio decoder could not read header metadata: " + absSourcePath;
        return r;
    }

    const double durationSeconds =
        static_cast<double>(lengthInFrames) / static_cast<double>(sampleRate);
    if (!std::isfinite(durationSeconds) || durationSeconds < 0.0) {
        r.error = "audio decoder reported invalid duration: " + absSourcePath;
        return r;
    }

    r.success = true;
    r.metadata = {
        {"durationSeconds", durationSeconds},
        {"channels", channels},
        {"sampleRate", sampleRate},
        {"loadMode", loadMode}
    };
    return r;
}

} // namespace molga
