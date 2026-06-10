#include "texture_rgba_renderer_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <sstream>

namespace texture_rgba_renderer {

namespace {

// Unregisters the texture asynchronously and keeps it alive until the engine
// confirms, so the raster thread can never touch a destroyed pixel buffer.
// The blocking single-argument UnregisterTexture overload must not be used
// on the platform thread: it latch-waits on the raster thread and can
// deadlock the whole app.
void UnregisterAndDestroy(flutter::TextureRegistrar *registrar,
                          std::unique_ptr<TextureRgba> texture) {
  auto texture_id = texture->texture_id();
  std::shared_ptr<TextureRgba> shared = std::move(texture);
  registrar->UnregisterTexture(texture_id, [shared]() {});
}

}  // namespace

// static
void TextureRgbaRendererPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "texture_rgba_renderer",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<TextureRgbaRendererPlugin>();
  plugin->texture_registrar = registrar->texture_registrar();

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

TextureRgbaRendererPlugin::TextureRgbaRendererPlugin() {
    this->texture_registrar = nullptr;
}

TextureRgbaRendererPlugin::~TextureRgbaRendererPlugin() {}

void TextureRgbaRendererPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name().compare("createTexture") == 0) {
     auto args = std::get<flutter::EncodableMap>(*method_call.arguments());
     auto key = std::get<int>(args.at(flutter::EncodableValue("key")));
     auto existing = textures_.find(key);
     if (existing != textures_.end()) {
         // Re-creating an existing key replaces the old texture. The previous
         // code left the MethodResult unanswered in this case, which hung the
         // awaiting Dart future forever.
         UnregisterAndDestroy(this->texture_registrar, std::move(existing->second));
         textures_.erase(existing);
     }
     auto texture = std::make_unique<TextureRgba>(this->texture_registrar);
     auto texture_id = texture->texture_id();
     textures_[key] = std::move(texture);
     result->Success(flutter::EncodableValue(texture_id));
  }
  else if (method_call.method_name().compare("closeTexture") == 0) {
      auto args = std::get<flutter::EncodableMap>(*method_call.arguments());
      auto key = std::get<int>(args.at(flutter::EncodableValue("key")));
      auto it = textures_.find(key);
      if (it == textures_.end()) {
          result->Success(flutter::EncodableValue(false));
      }
      else {
          UnregisterAndDestroy(this->texture_registrar, std::move(it->second));
          textures_.erase(it);
          result->Success(flutter::EncodableValue(true));
      }
  }
  else if (method_call.method_name().compare("onRgba") == 0) {
      auto args = std::get<flutter::EncodableMap>(*method_call.arguments());
      auto key = std::get<int>(args.at(flutter::EncodableValue("key")));
      if (textures_.find(key) == textures_.end()) {
          result->Success(flutter::EncodableValue(false));
      }
      else {
          auto& data = std::get<std::vector<uint8_t>>(args.at(flutter::EncodableValue("data")));
          auto height = std::get<int>(args.at(flutter::EncodableValue("height")));
          auto width = std::get<int>(args.at(flutter::EncodableValue("width")));
          textures_[key]->MarkVideoFrameAvailable(data, width, height);
          result->Success(flutter::EncodableValue(true));
      }
  } 
  else if (method_call.method_name().compare("getTexturePtr") == 0) {
      auto args = std::get<flutter::EncodableMap>(*method_call.arguments());
      auto key = std::get<int>(args.at(flutter::EncodableValue("key")));
      if (textures_.find(key) == textures_.end()) {
          result->Success(flutter::EncodableValue(-1));
      }
      else {
          // Return an address.
          size_t rgba = reinterpret_cast<size_t>((void*) textures_[key].get());
          result->Success(flutter::EncodableValue(int64_t(rgba)));
      }
  }
  else {
    result->NotImplemented();
  }
}

}  // namespace texture_rgba_renderer

