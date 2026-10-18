#include "cameras.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

size_t after(const std::string& s, const char* needle) {
    size_t p = s.find(needle);
    return p == std::string::npos ? std::string::npos : p + strlen(needle);
}

std::vector<int> parse_int_set(const std::string& s) {
    std::vector<int> out;
    size_t i = s.find('{');
    size_t j = s.find('}');
    if (i == std::string::npos || j == std::string::npos || j < i) return out;

    std::string body = s.substr(i + 1, j - i - 1);
    size_t pos = 0;
    while (pos < body.size()) {
        size_t comma = body.find(',', pos);
        std::string tok = trim(body.substr(pos, comma == std::string::npos ? comma : comma - pos));
        if (!tok.empty()) out.push_back(atoi(tok.c_str()));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

}

bool parse_camera_list(const std::string& output, std::vector<CameraInfo>* out) {
    if (!output.find("List of cameras:")) return false;

    out->clear();
    CameraInfo* cur = nullptr;

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        std::string line = trim(output.substr(pos, eol == std::string::npos ? eol : eol - pos));
        pos = (eol == std::string::npos) ? output.size() : eol + 1;

        if (line.empty()) continue;

        size_t idp = line.find("--camera-id=");
        if (idp != std::string::npos) {
            out->emplace_back();
            cur = &out->back();
            size_t id_end = line.find(' ', idp + 12);
            cur->id = line.substr(idp + 12, id_end - (idp + 12));
        }

        if (line.find("High speed capture") != std::string::npos) continue;

        if (line[0] == '-' && line.size() > 1 && line[1] == ' ') {
            if (!cur) continue;
            std::string rest = trim(line.substr(2));
            size_t x = rest.find('x');
            if (x == std::string::npos) continue;

            CameraSize s;
            s.w = atoi(rest.substr(0, x).c_str());
            s.h = atoi(rest.substr(x + 1).c_str());

            size_t fp = rest.find("fps=");
            if (fp != std::string::npos) {
                s.high_speed = true;
                s.fps = parse_int_set(rest.substr(fp));
                cur->high_speed_sizes.push_back(s);
            } else {
                cur->sizes.push_back(s);
            }
            continue;
        }

        if (!cur) continue;

        size_t paren = line.find('(');
        if (paren != std::string::npos) {
            std::string spec = line.substr(paren);
            size_t comma = spec.find(',');
            if (comma != std::string::npos) cur->facing = trim(spec.substr(1, comma - 1));

            size_t mm = spec.find("mm, ");
            size_t dim = (mm == std::string::npos) ? spec.find_first_of("0123456789")
                                                   : spec.find_first_of("0123456789", mm + 4);
if (mm != std::string::npos) {
                size_t start = mm;
                while (start > 0 && (isdigit((unsigned char)spec[start - 1]) ||
                                     spec[start - 1] == '.')) {
                    --start;
                }
                cur->focal_mm = (float)atof(spec.substr(start, mm - start).c_str());
            }
            if (dim != std::string::npos) {
                size_t x = spec.find('x', dim);
                if (x != std::string::npos) {
                    cur->sensor_w = atoi(spec.substr(dim, x - dim).c_str());
                    cur->sensor_h = atoi(spec.substr(x + 1).c_str());
                }
            }

            size_t fp = spec.find("fps=");
            if (fp != std::string::npos) cur->fps = parse_int_set(spec.substr(fp));

            size_t zp = spec.find("zoom-range=[");
            if (zp != std::string::npos) {
                cur->has_zoom = true;
                cur->zoom_min = (float)atof(spec.c_str() + zp + 12);
                size_t comma2 = spec.find(',', zp);
                if (comma2 != std::string::npos) {
                    cur->zoom_max = (float)atof(spec.c_str() + comma2 + 1);
                }
            }

            size_t fp2 = spec.find("focus-range=[");
            if (fp2 != std::string::npos) {
                cur->has_focus = true;
                cur->focus_min = (float)atof(spec.c_str() + fp2 + 13);
            }

            size_t ip = spec.find("iso-range=[");
            if (ip != std::string::npos) {
                cur->has_iso = true;
                size_t end = spec.find(']', ip);
                std::string body = spec.substr(ip + 11, (end == std::string::npos ? spec.size() : end) - (ip + 11));
                size_t c = body.find(',');
                if (c != std::string::npos) {
                    cur->iso_min = atoi(body.substr(0, c).c_str());
                    cur->iso_max = atoi(body.substr(c + 1).c_str());
                }
            }

            size_t ep = spec.find("exp-range=[");
            if (ep != std::string::npos) {
                cur->has_exp = true;
                size_t end = spec.find(']', ep);
                std::string body = spec.substr(ep + 11, (end == std::string::npos ? spec.size() : end) - (ep + 11));
                size_t c = body.find(',');
                if (c != std::string::npos) {
                    cur->exp_min_ns = atol(body.substr(0, c).c_str());
                    cur->exp_max_ns = atol(body.substr(c + 1).c_str());
                }
            }
            continue;
        }
    }

    return !out->empty();
}

int max_fps(const CameraInfo& cam) {
    int m = 0;
    for (int f : cam.fps) m = std::max(m, f);
    return m;
}
