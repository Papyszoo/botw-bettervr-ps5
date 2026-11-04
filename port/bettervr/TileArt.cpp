// SPDX-License-Identifier: GPL-3.0-or-later
// BotW VR (docs/bettervr-ps5): the app's home screen tile and background from the player's own Breath
// of the Wild, as the launcher found it (frontend/shell/shell.cpp, PollBotwArt). Nothing of the game's
// is in the app or its release: its box art (GameTDB's, else the game's icon, meta/iconTex) becomes
// sce_sys/icon0.png, and its boot screen (meta/bootTvTex, 1280 x 720) sce_sys/pic0.dds and pic1.dds,
// in the app's own folder, where the console reads them when it puts the app on the home screen.
// The background is a 3840 x 2160 BC7_UNORM DDS, as the app's own (tools/render-presentation.py, whose
// mode 6 encoder this is in C++).
//
// The pictures are made again only when their sources change (bettervr-art.txt in the launcher's
// folder). Whether the home screen shows them at once, or only after ShadowMountPlus puts the app on
// it again, is what Test request 5 asks.

#include "../app/paths.h"
#include "../ps5/log.h"
#include "../ui/images.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	constexpr int kIconSize = 512;
	constexpr int kWidth = 3840, kHeight = 2160;
	std::atomic<bool> s_running{false};

	std::vector<uint8_t> ReadFile(const std::string& path, size_t limit)
	{
		std::vector<uint8_t> data;
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return data;
		file.seekg(0, std::ios::end);
		const std::streamoff size = file.tellg();
		if (size <= 0 || (size_t)size > limit)
			return data;
		data.resize((size_t)size);
		file.seekg(0);
		file.read(reinterpret_cast<char*>(data.data()), size);
		if (!file)
			data.clear();
		return data;
	}

	// An uncompressed true-colour TGA (as app/covers.cpp writes the boot screen) as RGB, top-down
	bool DecodeTga(const std::vector<uint8_t>& data, int& width, int& height, std::vector<uint8_t>& rgb)
	{
		if (data.size() < 18 || data[1] != 0 || data[2] != 2 || (data[16] != 24 && data[16] != 32))
			return false;
		width = data[12] | (data[13] << 8);
		height = data[14] | (data[15] << 8);
		const int bytes = data[16] / 8;
		const bool topDown = data[17] & 0x20;
		const size_t offset = 18 + data[0];
		if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || data.size() < offset + (size_t)width * height * bytes)
			return false;
		rgb.resize((size_t)width * height * 3);
		for (int y = 0; y < height; y++)
		{
			const uint8_t* row = &data[offset + (size_t)(topDown ? y : height - 1 - y) * width * bytes];
			for (int x = 0; x < width; x++)
			{
				uint8_t* out = &rgb[((size_t)y * width + x) * 3];
				out[0] = row[x * bytes + 2];
				out[1] = row[x * bytes + 1];
				out[2] = row[x * bytes + 0];
			}
		}
		return true;
	}

	// source scaled to cover kWidth x kHeight (bilinear, centred, cropped)
	std::vector<uint8_t> Fill(const std::vector<uint8_t>& source, int width, int height)
	{
		std::vector<uint8_t> out((size_t)kWidth * kHeight * 3);
		const float scale = std::max((float)kWidth / width, (float)kHeight / height);
		const float offsetX = (width * scale - kWidth) / 2, offsetY = (height * scale - kHeight) / 2;
		for (int y = 0; y < kHeight; y++)
		{
			const float sy = std::clamp((y + offsetY + 0.5f) / scale - 0.5f, 0.0f, (float)height - 1);
			const int y0 = (int)sy, y1 = std::min(y0 + 1, height - 1);
			const float fy = sy - y0;
			for (int x = 0; x < kWidth; x++)
			{
				const float sx = std::clamp((x + offsetX + 0.5f) / scale - 0.5f, 0.0f, (float)width - 1);
				const int x0 = (int)sx, x1 = std::min(x0 + 1, width - 1);
				const float fx = sx - x0;
				for (int c = 0; c < 3; c++)
				{
					auto at = [&](int px, int py) { return (float)source[((size_t)py * width + px) * 3 + c]; };
					const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
					const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
					out[((size_t)y * kWidth + x) * 3 + c] = (uint8_t)std::lround(top + (bottom - top) * fy);
				}
			}
		}
		return out;
	}

	// BC7 mode 6, as tools/render-presentation.py's encode_bc7: per 4x4 block a line between two
	// colours along the block's principal axis (7 bits a channel, p-bits 1, alpha 255) and a 4-bit
	// position on it for each pixel
	std::vector<uint8_t> EncodeBc7(const std::vector<uint8_t>& rgb)
	{
		static constexpr int kWeights[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
		std::vector<uint8_t> out((size_t)(kWidth / 4) * (kHeight / 4) * 16);
		size_t written = 0;
		for (int by = 0; by < kHeight; by += 4)
			for (int bx = 0; bx < kWidth; bx += 4)
			{
				double px[16][3], mean[3] = {};
				for (int i = 0; i < 16; i++)
					for (int c = 0; c < 3; c++)
					{
						px[i][c] = rgb[((size_t)(by + i / 4) * kWidth + bx + i % 4) * 3 + c];
						mean[c] += px[i][c] / 16;
					}
				double cov[3][3] = {};
				for (int i = 0; i < 16; i++)
					for (int a = 0; a < 3; a++)
						for (int b = 0; b < 3; b++)
							cov[a][b] += (px[i][a] - mean[a]) * (px[i][b] - mean[b]);
				double axis[3] = {0.57735, 0.57735, 0.57735};
				for (int step = 0; step < 8; step++)
				{
					double v[3];
					for (int a = 0; a < 3; a++)
						v[a] = cov[a][0] * axis[0] + cov[a][1] * axis[1] + cov[a][2] * axis[2];
					const double norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
					if (norm > 1e-9)
						for (int a = 0; a < 3; a++)
							axis[a] = v[a] / norm;
				}
				double tMin = 1e30, tMax = -1e30;
				for (int i = 0; i < 16; i++)
				{
					const double t = (px[i][0] - mean[0]) * axis[0] + (px[i][1] - mean[1]) * axis[1] + (px[i][2] - mean[2]) * axis[2];
					tMin = std::min(tMin, t);
					tMax = std::max(tMax, t);
				}
				int q[2][3], r[2][3];
				for (int c = 0; c < 3; c++)
				{
					const double ends[2] = {std::clamp(mean[c] + tMin * axis[c], 0.0, 255.0), std::clamp(mean[c] + tMax * axis[c], 0.0, 255.0)};
					for (int e = 0; e < 2; e++)
					{
						q[e][c] = (int)std::clamp(std::nearbyint((ends[e] - 1.0) / 2.0), 0.0, 127.0);
						r[e][c] = q[e][c] * 2 + 1;
					}
				}
				const double line[3] = {(double)(r[1][0] - r[0][0]), (double)(r[1][1] - r[0][1]), (double)(r[1][2] - r[0][2])};
				const double length = line[0] * line[0] + line[1] * line[1] + line[2] * line[2];
				int index[16] = {};
				if (length > 0)
					for (int i = 0; i < 16; i++)
					{
						const double pos = ((px[i][0] - r[0][0]) * line[0] + (px[i][1] - r[0][1]) * line[1] + (px[i][2] - r[0][2]) * line[2]) / length * 64.0;
						int best = 0;
						for (int k = 1; k < 16; k++)
							if (std::abs(pos - kWeights[k]) < std::abs(pos - kWeights[best]))
								best = k;
						index[i] = best;
					}
				// the first pixel's index must have its top bit clear: else the ends swap
				if (index[0] >= 8)
				{
					for (int c = 0; c < 3; c++)
						std::swap(q[0][c], q[1][c]);
					for (int& i : index)
						i = 15 - i;
				}
				uint64_t lo = 1ull << 6; // mode 6
				for (int c = 0; c < 3; c++)
				{
					lo |= (uint64_t)q[0][c] << (7 + 14 * c);
					lo |= (uint64_t)q[1][c] << (14 + 14 * c);
				}
				lo |= 127ull << 49;
				lo |= 127ull << 56;
				lo |= 1ull << 63; // P0
				uint64_t hi = 1; // P1
				hi |= (uint64_t)index[0] << 1;
				for (int i = 1; i < 16; i++)
					hi |= (uint64_t)index[i] << (4 * i);
				for (int b = 0; b < 8; b++)
				{
					out[written + b] = (uint8_t)(lo >> (8 * b));
					out[written + 8 + b] = (uint8_t)(hi >> (8 * b));
				}
				written += 16;
			}
		return out;
	}

	// tools/render-presentation.py's write_dds: the DX10 header, BC7_UNORM, no mipmaps
	std::vector<uint8_t> Dds(const std::vector<uint8_t>& blocks)
	{
		std::vector<uint8_t> out;
		auto u32 = [&](uint32_t v) {
			for (int b = 0; b < 4; b++)
				out.push_back((uint8_t)(v >> (8 * b)));
		};
		out.insert(out.end(), {'D', 'D', 'S', ' '});
		u32(124);
		u32(0xA1007);
		u32(kHeight);
		u32(kWidth);
		u32((uint32_t)blocks.size());
		u32(0);
		u32(1);
		out.resize(out.size() + 44);
		u32(32);
		u32(0x4);
		out.insert(out.end(), {'D', 'X', '1', '0'});
		for (int i = 0; i < 5; i++)
			u32(0);
		u32(0x1000);
		for (int i = 0; i < 4; i++)
			u32(0);
		u32(98); // DXGI_FORMAT_BC7_UNORM
		u32(3);	 // a 2D texture
		u32(0);
		u32(1);
		u32(0);
		out.insert(out.end(), blocks.begin(), blocks.end());
		return out;
	}

	void Append(void* context, void* data, int size)
	{
		auto* out = static_cast<std::vector<uint8_t>*>(context);
		out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
	}

	// path written whole, through a temporary file beside it
	bool Write(const std::string& path, const std::vector<uint8_t>& data)
	{
		const std::string temporary = path + ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file)
				return false;
			file.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
			if (!file)
				return false;
		}
		std::error_code ec;
		fs::rename(temporary, path, ec);
		if (ec)
			fs::remove(temporary, ec);
		chmod(path.c_str(), 0777); // as everything in the app's folder (tools/package.sh)
		return !ec;
	}

	std::string Signature(const std::string& path)
	{
		std::error_code ec;
		const auto size = fs::file_size(path, ec);
		return path + " " + (ec ? std::string("?") : std::to_string(size));
	}

	// The app's folder as it is on the drive (the one the console mounts it from), not /app0
	std::string WritableAppDir()
	{
		if (std::string source = ps5paths::MountSource(); !source.empty())
			return source;
		const std::string found = ps5paths::AppDir();
		return found == "/app0" || found.rfind("/system_ex/", 0) == 0 || found.rfind("/mnt/sandbox/", 0) == 0 ? std::string(ps5paths::kInstallDir) : found;
	}

	void Make(std::string cover, std::string bootScreen)
	{
		const std::string marker = ps5paths::Root() + "/bettervr-art.txt";
		const std::string wanted = Signature(cover) + "\n" + Signature(bootScreen) + "\n";
		if (ReadFile(marker, 4096) == std::vector<uint8_t>(wanted.begin(), wanted.end()))
		{
			ps5log::Line("[vr] home screen pictures: made already from these ({})", marker);
			return;
		}
		const std::string sceSys = WritableAppDir() + "/sce_sys";
		bool made = false;
		std::vector<uint8_t> rgba;
		if (!cover.empty() && !ui::SquarePicture(cover, kIconSize, rgba))
			ps5log::Line("[vr] home screen tile: {} could not be read", cover);
		else if (!cover.empty())
		{
			std::vector<uint8_t> rgb((size_t)kIconSize * kIconSize * 3), png;
			for (size_t i = 0, o = 0; i < rgba.size(); i += 4, o += 3)
				std::memcpy(&rgb[o], &rgba[i], 3);
			if (stbi_write_png_to_func(Append, &png, kIconSize, kIconSize, 3, rgb.data(), kIconSize * 3) && !png.empty())
			{
				const bool icon = Write(sceSys + "/icon0.png", png);
				ps5log::Line("[vr] home screen tile from {}: {}", cover, icon ? "written" : "not written (no write access to " + sceSys + ")");
				made |= icon;
				// the console's own copy of the app's icon, where it keeps one (needs root: the HEN's)
				const std::string appMeta = std::string("/user/appmeta/") + ps5paths::kTitleId;
				std::error_code ec;
				if (fs::is_directory(appMeta, ec))
					ps5log::Line("[vr] {}/icon0.png: {}", appMeta, Write(appMeta + "/icon0.png", png) ? "written" : "not written");
			}
		}
		int width = 0, height = 0;
		std::vector<uint8_t> rgb;
		if (!bootScreen.empty() && !DecodeTga(ReadFile(bootScreen, 16u << 20), width, height, rgb))
			ps5log::Line("[vr] home screen background: {} could not be read", bootScreen);
		else if (!bootScreen.empty())
		{
			const std::vector<uint8_t> dds = Dds(EncodeBc7(Fill(rgb, width, height)));
			const bool pic0 = Write(sceSys + "/pic0.dds", dds), pic1 = Write(sceSys + "/pic1.dds", dds);
			ps5log::Line("[vr] home screen background from {} ({}x{}): {}", bootScreen, width, height, pic0 && pic1 ? "written" : "not written");
			made |= pic0 && pic1;
		}
		if (made)
			Write(marker, std::vector<uint8_t>(wanted.begin(), wanted.end()));
	}
}

// From the launcher (frontend/shell/shell.cpp): cover is the game's box art or icon, bootScreen its
// boot screen's TGA; either may be empty. On a thread of its own, once at a time.
void PS5Cemu_BotwTileArt(const std::string& cover, const std::string& bootScreen)
{
	if (s_running.exchange(true))
		return;
	std::thread([cover, bootScreen] {
		Make(cover, bootScreen);
		s_running = false;
	}).detach();
}
