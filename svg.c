#include "svg.h"
#include "regression.h"
#include "scale.h"
#include "counter.h"

/* SVG generation. */

#define REGRESSION_LINE_WIDTH 2

/* Spacing and sizing constants for tick labels and axis labels */
#define TICK_LABEL_FONT_SIZE 10
#define AXIS_LABEL_FONT_SIZE 12
#define TICK_LABEL_OFFSET_X 3        /* Spacing between X-axis tick and its label */
#define TICK_LABEL_OFFSET_Y 3        /* Spacing between Y-axis tick and its label */
#define TICK_LABEL_MARGIN_X 15       /* Space reserved for X-axis tick labels below axis */
#define TICK_LABEL_MARGIN_Y 40       /* Space reserved for Y-axis tick labels left of axis */
#define AXIS_LABEL_MARGIN_X 50       /* Additional space for X-axis title label */
#define AXIS_LABEL_MARGIN_Y 50       /* Additional space for Y-axis title label */

static char *get_color(uint8_t column, svg_theme *theme);
static void svg_printf_header(size_t w, size_t h);
static void svg_printf_frame(size_t w, size_t h, char *fill_color, size_t border_width, char *border_color);
static void svg_printf_begin_polyline(void);
static void svg_printf_polyline_point(size_t x, size_t y);
static void svg_printf_end_polyline(char *color, size_t line_width);
static void svg_printf_circle(size_t x, size_t y, size_t point_size, char *color);
static void svg_printf_axis(config *cfg, plot_info *pi, svg_theme *theme);
static void svg_printf_axis_labels(config *cfg, plot_info *pi, size_t svg_width, size_t svg_height, size_t label_space_left, size_t label_space_bottom, svg_theme *theme);
static void svg_printf_regression_line(plot_info *pi, char *color, double slope, double intercept);
static void svg_printf_end(void);
static void format_number(double value, char *buf, size_t buflen, double range);
static double pixel_to_data_value(int pixel_pos, size_t axis_pixel, double min_val, double range, size_t dimension, bool is_log);

int svg_plot(config *cfg, plot_info *pi, data_set *ds) {
    svg_theme *theme = cfg->svg_theme;
    
    /* Calculate extra space needed for labels */
    size_t label_space_left = 0;
    size_t label_space_bottom = 0;
    
    /* Space for tick labels (if enabled) */
    if (cfg->axis && cfg->axis_tick_labels) {
        label_space_left = TICK_LABEL_MARGIN_Y;
        label_space_bottom = TICK_LABEL_MARGIN_X;
    }
    
    /* Additional space for axis title labels */
    if (cfg->axis && cfg->y_axis_label != NULL) {
        label_space_left += AXIS_LABEL_MARGIN_Y;
    }
    if (cfg->axis && cfg->x_axis_label != NULL) {
        label_space_bottom += AXIS_LABEL_MARGIN_X;
    }
    
    /* Expanded SVG viewport dimensions */
    size_t svg_width = pi->w + label_space_left;
    size_t svg_height = pi->h + label_space_bottom;
    
    svg_printf_header(svg_width, svg_height);
    
    /* Add full-viewport background rect to ensure consistent background color */
    printf("<rect x=\"0\" y=\"0\" width=\"%zu\" height=\"%zu\" fill=\"%s\" />\n",
        svg_width, svg_height, theme->bg_color);
    
    /* Use a group with transform to offset the plot area to make room for Y-axis label */
    if (label_space_left > 0) {
        printf("<g transform=\"translate(%zu,0)\">\n", label_space_left);
    }
    
    svg_printf_frame(pi->w, pi->h, theme->bg_color, theme->border_width, theme->border_color);

    if (cfg->axis) {
        draw_calc_axis_pos(pi);
        svg_printf_axis(cfg, pi, theme);
    }
    
    if (label_space_left > 0) {
        printf("</g>\n");
    }

    transform_t transform = scale_get_transform(pi->log_x, pi->log_y);

    /* Wrap data points/lines in the same transform group */
    if (label_space_left > 0) {
        printf("<g transform=\"translate(%zu,0)\">\n", label_space_left);
    }

    for (uint8_t c = 0; c < ds->columns; c++) {
        char *color = get_color(c, theme);
        point *column = ds->pairs[c];

        if (cfg->mode == MODE_LINE) {
            bool beginning_line = true;
            for (size_t i = 0; i < ds->rows; i++) {
                point *p = &column[i];
                if (IS_EMPTY(p->x) || IS_EMPTY(p->y)) {
                    if (!beginning_line) {
                        svg_printf_end_polyline(color, theme->line_width);
                    }
                    beginning_line = true;
                    continue;
                }

                if (beginning_line) { 
                    svg_printf_begin_polyline();
                    beginning_line = false;
                }
                scaled_point sp;
                scale_point(pi, p, &sp, transform);
                svg_printf_polyline_point(sp.x, sp.y);
            }
            svg_printf_end_polyline(color, theme->line_width);
        } else {
            for (size_t i = 0; i < ds->rows; i++) {
                point *p = &column[i];
                scaled_point sp;
                scale_point(pi, p, &sp, transform);

                if (IS_EMPTY(p->x) || IS_EMPTY(p->y)) { continue; }
                size_t point_size = SVG_DEF_POINT_SIZE;
                if (pi->counters) {
                    size_t count = counter_get(pi->counters[c], sp.x, sp.y);
                    point_size = SVG_DEF_POINT_SIZE + (cfg->log_count ? log(count) : count);
                }
                svg_printf_circle(sp.x, sp.y, point_size, color);
            }
        }

        if (cfg->regression) {
            double slope = 0;
            double intercept = 0;
            
            regression(column, ds->rows, transform, &slope, &intercept);
            svg_printf_regression_line(pi, color, slope, intercept);
        }
    }

    if (label_space_left > 0) {
        printf("</g>\n");
    }

    /* Add axis labels - these should be in the expanded viewport, not the transformed group */
    if (cfg->axis) {
        svg_printf_axis_labels(cfg, pi, svg_width, svg_height, label_space_left, label_space_bottom, theme);
    }

    svg_printf_end();
    return 0;
}

static char *get_color(uint8_t column, svg_theme *theme) {
    if (column < SVG_COLOR_COUNT) {
        return theme->colors[column];
    } else {
        return theme->colors[SVG_COLOR_COUNT - 1];
    }
}

static void svg_printf_header(size_t w, size_t h) {
    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" "
           "width=\"%zu\" height=\"%zu\" version=\"1.1\">\n",
        w, h);
    printf("<!-- Generator: guff %u.%u.%u -->\n",
        GUFF_VERSION_MAJOR, GUFF_VERSION_MINOR, GUFF_VERSION_PATCH);
}

static void svg_printf_frame(size_t w, size_t h, char *fill_color,
        size_t border_width, char *border_color) {
    printf("<rect x=\"0\" y=\"0\" width=\"%zu\" height=\"%zu\"\n",
        w, h);
    printf("    fill=\"%s\" stroke-width=\"%zu\" stroke=\"%s\" />\n",
        fill_color, border_width, border_color);
}

static void svg_printf_begin_polyline(void) {
    printf("<polyline points=\"\n");
}

static void svg_printf_polyline_point(size_t x, size_t y) {
    printf("    %zu,%zu\n", x, y);
}

static void svg_printf_end_polyline(char *color, size_t line_width) {
    printf("\" stroke=\"%s\" stroke-width=\"%zu\" fill=\"none\" />\n",
        color, line_width);
}

static void svg_printf_circle(size_t x, size_t y, size_t point_size, char *color) {
    printf("<circle cx=\"%zu\" cy=\"%zu\" r=\"%zu\" stroke=\"%s\" />\n",
        x, y, point_size, color);
}

static void format_number(double value, char *buf, size_t buflen, double range) {
    /* Format numbers with exponential notation for very large/small values */
    double abs_val = fabs(value);
    
    /* Use exponential notation for very large or very small numbers */
    if (abs_val >= 1e4 || (abs_val < 1e-2 && abs_val != 0.0)) {
        snprintf(buf, buflen, "%.1e", value);
    } else {
        /* Determine decimal places based on range */
        int decimals;
        if (range > 1000) {
            decimals = 0;
        } else if (range > 10) {
            decimals = 1;
        } else if (range > 1) {
            decimals = 2;
        } else {
            decimals = 3;
        }
        
        /* Format with appropriate precision, then remove trailing zeros */
        snprintf(buf, buflen, "%.*f", decimals, value);
        
        /* Remove trailing zeros and decimal point if not needed */
        if (strchr(buf, '.')) {
            char *end = buf + strlen(buf) - 1;
            while (end > buf && *end == '0') {
                *end = '\0';
                end--;
            }
            if (end > buf && *end == '.') {
                *end = '\0';
            }
        }
    }
}

static double pixel_to_data_value(int pixel_pos, size_t axis_pixel, 
                                   double min_val, double range, 
                                   size_t dimension, bool is_log) {
    /* Convert pixel position to data value */
    /* Note: For Y axis, pixels increase downward but data increases upward */
    double pixel_offset = (double)(pixel_pos - axis_pixel);
    double ratio = pixel_offset / (double)dimension;
    double data_value = min_val + (ratio * range);
    
    /* If logarithmic scale, convert back from log space */
    if (is_log) {
        data_value = exp(data_value);
    }
    
    return data_value;
}

static double scale_tick(size_t width, double range) {
    /* Return a size that divides the range to add roughly 5-10 ticks. */
    double rounded = pow(10, ceil(log10(range)));
    double step = rounded / (range < rounded / 2 ? 20 : 10);
    return width * (step / range);
}

static void svg_printf_axis(config *cfg, plot_info *pi, svg_theme *theme) {
    int tick_w = 3*theme->axis_width;

    // Y axis
    printf("<line x1=\"%zu\" y1=\"%d\" x2=\"%zu\" y2=\"%zu\" "
        "stroke=\"%s\" stroke-width=\"%u\" %s/>\n",
        pi->axis_x, 0, pi->axis_x, pi->h,
        theme->axis_color, theme->axis_width, pi->draw_y_axis ? "" : "stroke-dasharray=\"2,5\" ");

    // X axis ticks
    if (pi->draw_x_axis) {
        int y0 = pi->axis_y - tick_w;
        int y1 = pi->axis_y + tick_w;

        double xto = scale_tick(pi->w, pi->range_x);
        for (int wx = pi->axis_x + xto; wx < pi->w; wx += xto) {
            printf("<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                "stroke=\"%s\" stroke-width=\"1\" />\n",
                wx, y0, wx, y1, theme->axis_color);
            
            /* Add numeric label if enabled */
            if (cfg->axis_tick_labels) {
                double data_value = pixel_to_data_value(wx, pi->axis_x, 
                                                        pi->min_x, pi->range_x, 
                                                        pi->w, pi->log_x);
                char label[32];
                format_number(data_value, label, sizeof(label), pi->range_x);
                int label_y = pi->axis_y + tick_w + 15;
                printf("<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" "
                       "fill=\"%s\" font-family=\"sans-serif\" font-size=\"10\">%s</text>\n",
                       wx, label_y, theme->axis_color, label);
            }
        }
        for (int wx = pi->axis_x - xto; wx > 0; wx -= xto) {
            printf("<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                "stroke=\"%s\" stroke-width=\"1\" />\n",
                wx, y0, wx, y1, theme->axis_color);
            
            /* Add numeric label if enabled */
            if (cfg->axis_tick_labels) {
                double data_value = pixel_to_data_value(wx, pi->axis_x, 
                                                        pi->min_x, pi->range_x, 
                                                        pi->w, pi->log_x);
                char label[32];
                format_number(data_value, label, sizeof(label), pi->range_x);
                int label_y = pi->axis_y + tick_w + 15;
                printf("<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" "
                       "fill=\"%s\" font-family=\"sans-serif\" font-size=\"10\">%s</text>\n",
                       wx, label_y, theme->axis_color, label);
            }
        }
    }

    // X axis
    printf("<line x1=\"%zu\" y1=\"%zu\" x2=\"%zu\" y2=\"%zu\" "
        "stroke=\"%s\" stroke-width=\"%u\" %s/>\n",
        0L, pi->axis_y, pi->w, pi->axis_y,
        theme->axis_color, theme->axis_width, pi->draw_x_axis ? "" : "stroke-dasharray=\"2,5\" ");

    // Y axis ticks
    if (pi->draw_y_axis) {
        int x0 = pi->axis_x - tick_w;
        int x1 = pi->axis_x + tick_w;
        if (x0 > pi->w) { x0 = 0; }  // don't wrap

        double yto = scale_tick(pi->h, pi->range_y);
        for (int hy = pi->axis_y + yto; hy < pi->h; hy += yto) {
            printf("<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                "stroke=\"%s\" stroke-width=\"1\" />\n",
                x0, hy, x1, hy, theme->axis_color);
            
            /* Add numeric label if enabled */
            if (cfg->axis_tick_labels) {
                /* Y-axis: pixels go down, but data values go up, so we need to invert */
                double data_value = pixel_to_data_value(pi->h - hy, pi->h - pi->axis_y,
                                                        pi->min_y, pi->range_y,
                                                        pi->h, pi->log_y);
                char label[32];
                format_number(data_value, label, sizeof(label), pi->range_y);
                int label_x = pi->axis_x - tick_w - 8;
                printf("<text x=\"%d\" y=\"%d\" text-anchor=\"end\" "
                       "fill=\"%s\" font-family=\"sans-serif\" font-size=\"10\">%s</text>\n",
                       label_x, hy + 4, theme->axis_color, label);
            }
        }
        for (int hy = pi->axis_y - yto; hy > 0; hy -= yto) {
            printf("<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                "stroke=\"%s\" stroke-width=\"1\" />\n",
                x0, hy, x1, hy, theme->axis_color);
            
            /* Add numeric label if enabled */
            if (cfg->axis_tick_labels) {
                /* Y-axis: pixels go down, but data values go up, so we need to invert */
                double data_value = pixel_to_data_value(pi->h - hy, pi->h - pi->axis_y,
                                                        pi->min_y, pi->range_y,
                                                        pi->h, pi->log_y);
                char label[32];
                format_number(data_value, label, sizeof(label), pi->range_y);
                int label_x = pi->axis_x - tick_w - 8;
                printf("<text x=\"%d\" y=\"%d\" text-anchor=\"end\" "
                       "fill=\"%s\" font-family=\"sans-serif\" font-size=\"10\">%s</text>\n",
                       label_x, hy + 4, theme->axis_color, label);
            }
        }
    }
}

static void svg_printf_axis_labels(config *cfg, plot_info *pi, size_t svg_width, size_t svg_height, size_t label_space_left, size_t label_space_bottom, svg_theme *theme) {
    /* X-axis label: centered horizontally in the plot area, positioned in the bottom margin */
    if (cfg->x_axis_label != NULL) {
        /* Center the label horizontally in the plot area (accounting for left offset) */
        size_t label_x = label_space_left + (pi->w / 2);
        /* Position in the bottom margin, nicely spaced */
        size_t label_y = pi->h + (label_space_bottom / 2) + 5;  /* Centered in bottom margin */
        printf("<text x=\"%zu\" y=\"%zu\" text-anchor=\"middle\" "
            "fill=\"%s\" font-family=\"sans-serif\" font-size=\"12\">%s</text>\n",
            label_x, label_y, theme->axis_color, cfg->x_axis_label);
    }

    /* Y-axis label: rotated 90° counterclockwise, centered vertically, in the left margin */
    if (cfg->y_axis_label != NULL) {
        /* Position in the left margin */
        size_t label_x = label_space_left / 2;
        /* Center vertically in the plot area */
        size_t label_y = pi->h / 2;
        printf("<text x=\"%zu\" y=\"%zu\" text-anchor=\"middle\" "
            "fill=\"%s\" font-family=\"sans-serif\" font-size=\"12\" "
	    "transform=\"rotate(-90 %zu %zu)\">%s</text>\n",
            label_x, label_y, theme->axis_color, label_x, label_y, cfg->y_axis_label);
    }
}

static void svg_printf_end(void) {
    printf("</svg>\n");
}

static void svg_printf_regression_line(plot_info *pi, char *color,
        double slope, double intercept) {

    point p0 = { .x = pi->min_x, .y = slope * pi->min_x + intercept };
    point p1 = { .x = pi->max_x, .y = slope * pi->max_x + intercept };
    LOG(2, "p0: %g * %g + %g => %g\n", slope, pi->min_x, intercept, p0.y);
    LOG(2, "p1: %g * %g + %g => %g\n", slope, pi->max_x, intercept, p1.y);

    scaled_point sp0, sp1;
    transform_t t = TRANSFORM_NONE;  // already transformed
    scale_point(pi, &p0, &sp0, t);
    scale_point(pi, &p1, &sp1, t);

    LOG(2, "p0: (%g, %g) => [%d, %d]\n", p0.x, p0.y, sp0.x, sp0.y);
    LOG(2, "p1: (%g, %g) => [%d, %d]\n", p1.x, p1.y, sp1.x, sp1.y);

    printf("<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
        "stroke=\"%s\" stroke-width=\"%u\" stroke-dasharray=\"2,5\" />\n",
        sp0.x, sp0.y, sp1.x, sp1.y, color, REGRESSION_LINE_WIDTH);
}
