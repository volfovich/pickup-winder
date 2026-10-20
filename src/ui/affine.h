#pragma once

// Touch calibration math: maps raw touch readings to screen pixels with an
// affine transform solved from three reference points. Pure C++ (no Arduino)
// so it can be tested on the host.

namespace ui
{
    struct Affine
    {
        // screenX = a*rawX + b*rawY + c,  screenY = d*rawX + e*rawY + f
        float a, b, c, d, e, f;

        float mapX(float rx, float ry) const { return a * rx + b * ry + c; }
        float mapY(float rx, float ry) const { return d * rx + e * ry + f; }
    };

    // Solves the transform from raw points (rx[i], ry[i]) that were touched at
    // screen points (sx[i], sy[i]). Returns false if the raw points are
    // (nearly) collinear, i.e. the touches were bad.
    inline bool solveAffine(const float rx[3], const float ry[3], const float sx[3], const float sy[3], Affine& out)
    {
        // Cramer's rule on [rx ry 1] * [a b c]^T = s
        double det = (double)rx[0] * (ry[1] - ry[2]) - (double)ry[0] * (rx[1] - rx[2]) + ((double)rx[1] * ry[2] - (double)rx[2] * ry[1]);
        if (det > -1e3 && det < 1e3) return false; // raw units are 0..4095, real triangles give |det| >> 1e5

        auto solve = [&](const float s[3], float& p, float& q, float& r) {
            p = ((double)s[0] * (ry[1] - ry[2]) - (double)ry[0] * (s[1] - s[2]) + ((double)s[1] * ry[2] - (double)s[2] * ry[1])) / det;
            q = ((double)rx[0] * (s[1] - s[2]) - (double)s[0] * (rx[1] - rx[2]) + ((double)rx[1] * s[2] - (double)rx[2] * s[1])) / det;
            r = ((double)rx[0] * ((double)ry[1] * s[2] - (double)ry[2] * s[1]) - (double)ry[0] * ((double)rx[1] * s[2] - (double)rx[2] * s[1]) +
                 (double)s[0] * ((double)rx[1] * ry[2] - (double)rx[2] * ry[1])) /
                det;
        };
        solve(sx, out.a, out.b, out.c);
        solve(sy, out.d, out.e, out.f);
        return true;
    }
}
