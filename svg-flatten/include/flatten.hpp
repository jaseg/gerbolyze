
#include "gerbolyze.hpp"

namespace gerbolyze {
    class curve4_div {
        public:
            curve4_div(double distance_tolerance=0.1, double angle_tolerance=0.0, double cusp_limit=0.0)
                : m_cusp_limit(cusp_limit),
                m_distance_tolerance_square(0.25*distance_tolerance*distance_tolerance),
                m_angle_tolerance(angle_tolerance)
                {
                }

            void run(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4);
            const std::vector<d2p> &points() { return m_points; }

        private:
            void recursive_bezier(double x1, double y1, double x2, double y2, 
                                  double x3, double y3, double x4, double y4,
                                  unsigned level);
            double m_cusp_limit;
            double m_distance_tolerance_square;
            double m_angle_tolerance;
            std::vector<d2p> m_points;
    };
}

