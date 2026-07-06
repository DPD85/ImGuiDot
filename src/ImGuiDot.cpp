#include "ImGuiDot.h"

#include "ImGuiDot_Structs.h"

#include <algorithm>
#include <cgraph.h>
#include <colorprocs.h>
#include <cstring>
#include <gvc.h>
#include <gvplugin.h>
#include <imgui.h>
#include <limits>
#include <string>

namespace ImGuiDot
{
    // ----- Constants -----

    // Number of typographic points per inch.
    static const constexpr float PPI = 72.0f;

    // Number of pixels per typographic point.
    // Since:
    //  - one typographic point is 1/72 of inch;
    //  - DPI is number of point per inch (printer);
    //  - the digital screens always have one point for pixel;
    // then:
    //  number of pixels = (typographic points) * (DPI / 72).
    static const constexpr float PIXEL_PER_PPI = 96.0f / PPI;

    // ----- Global variables to use the Graphviz plugins -----

    extern "C"
    {
        extern gvplugin_library_t gvplugin_core_LTX_library;
        extern gvplugin_library_t gvplugin_dot_layout_LTX_library;
    }

    static constexpr lt_symlist_t gvPlugins[] = { { "gvplugin_core_LTX_library", &gvplugin_core_LTX_library },
                                                  { "gvplugin_dot_layout_LTX_library",
                                                    &gvplugin_dot_layout_LTX_library },
                                                  { 0, 0 } };

    // ----- Structure and data used to make Graphviz read from a string not null terminated -----

    struct MemoryData
    {
        const char *data;
        size_t len;
        size_t cur;
    };

    static int MemoryReader(void *chan, char *buf, int bufsize)
    {
        if (bufsize == 0) return 0;

        auto *memoryData = reinterpret_cast<MemoryData *>(chan);
        if (memoryData->cur >= memoryData->len) return 0;

        const size_t bytesToCopy = std::min<size_t>(memoryData->len - memoryData->cur, bufsize);

        memcpy(buf, memoryData->data + memoryData->cur, bytesToCopy);

        memoryData->cur += bytesToCopy;

        return bytesToCopy;
    }

    // ----- -----

    /// @brief Graphviz context.
    static GVC_t *gvContext = nullptr;

    /// @brief Internal state and parameters shared between various functions.
    struct Parameters
    {
        Agraph_t *graph;
        float zoom;
        Vec2 diagramPos; // [pixel]
    };

    // ----- -----

    static void DrawNodes(const Parameters &params);
    static void DrawArcs(const Parameters &params, Agnode_t *const node);
    static void DrawArrowTip(const Parameters &params, const Vec2 &apex, const Vec2 &from, const ImU32 color);
    static void DrawLabel(
        const Parameters &params,
        const textlabel_t *const label,
        const ImU32 defaultColor,
        const pointf *const position = nullptr);
    static Vec2 ConvertPoint(const Parameters &params, const Vec2 &point);
    static Color ExtractColor(void *object, const char *name, const ImColor defaultColor);
    static Color ExtractColor(const char *color, const ImColor defaultColor);

    // ----- -----

    bool Initialize()
    {
        gvContext = gvContextPlugins(gvPlugins, 0);
        return gvContext != nullptr;
    }

    void CleanUp()
    {
        gvFreeContext(gvContext);
    }

    void Diagram(const char *const code, const char *endCode, const float zoom)
    {
        DiagramState diagram;

        Update(diagram, code, endCode);
        Draw(diagram, zoom);
        CleanUp(diagram);
    }

    void Diagram(const std::string &code, const float zoom)
    {
        Diagram(code.data(), code.data() + code.size(), zoom);
    }

#if __cpp_lib_string_view
    void Diagram(const std::string_view &code, const float zoom)
    {
        Diagram(code.data(), code.data() + code.size(), zoom);
    }
#endif

    void Update(DiagramState &diagram, const char *const code, const char *endCode)
    {
        // ----- Construct and layout of the diagram

        if (endCode == nullptr) endCode = code + std::strlen(code);

        MemoryData input{ /*.data =*/code, /*.len =*/static_cast<size_t>(endCode - code), /*.cur =*/0 };
        Agiodisc_t iodisc{
            /*.afread = */ MemoryReader,
            /*.putstr = */ nullptr, // used only by gvRender() and the last one is not uses.
            /*.flush  = */ nullptr  // used only by gvRender() and the last one is not uses.
        };
        Agdisc_t disc{ /*.id =*/nullptr, /*.io =*/&iodisc };
        Agraph_t *newGraph = agread(&input, &disc);

        if (!newGraph)
        {
            // Error parsing the code so keep the previous diagram.
        }
        else
        {
            if (diagram.graph)
            {
                gvFreeLayout(gvContext, diagram.graph);
                agclose(diagram.graph);
            }
            gvLayout(gvContext, newGraph, "dot");
            diagram.graph = newGraph;
        }
    }

    void Update(DiagramState &diagram, const std::string &code)
    {
        Update(diagram, code.data(), code.data() + code.size());
    }

#if __cpp_lib_string_view
    void Update(DiagramState &diagram, const std::string_view &code)
    {
        Update(diagram, code.data(), code.data() + code.size());
    }
#endif

    void CleanUp(DiagramState &diagram)
    {
        if (diagram.graph)
        {
            gvFreeLayout(gvContext, diagram.graph);
            agclose(diagram.graph);
            diagram.graph = nullptr;
        }
    }

    void Draw(const DiagramState &diagram, const float zoom)
    {
        if (diagram.graph == nullptr) return;

        Parameters params{ /*.graph =*/diagram.graph, /*.zoom =*/zoom };

        // -----

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const Vec2 cursorPos   = ImGui::GetCursorScreenPos();
        const Vec2 spaceAvail  = ImGui::GetContentRegionAvail();

        // GD_bb(graph) give the diagram bounding box where:
        //   .LL = bounding box coordinate of the min vertex.
        //   .UR = bounding box coordinate of the max vertex.
        const Vec2 size = Vec2(GD_bb(params.graph).UR) * PIXEL_PER_PPI * zoom;

        // ----- Diagram alignment

        // Alignment to the centre.
        // params.diagramPos.x = cursorPos.x + (spaceAvail.x - size.x) / 2.0f;
        // params.diagramPos.y = cursorPos.y;

        // Alignment to the right.
        // params.diagramPos.x = cursorPos.x + (spaceAvail.x - size.x);
        // params.diagramPos.y = cursorPos.y;

        // Alignment to the left.
        params.diagramPos = cursorPos;

        // ----- Draw diagram background

        {
            const Color color = ExtractColor(params.graph, "bgcolor", IM_COL32(255, 255, 255, 255));
            if (color.isValid)
            {
                const Vec2 min = params.diagramPos;
                const Vec2 max = params.diagramPos + size;
                draw->AddRectFilled(min, max, color.color);
            }
        }

        // -----

        DrawNodes(params);
    }

    // ----- -----

    /// @brief Draw all the nodes of a diagram.
    /// @param params The internal state and parameters to use.
    static void DrawNodes(const Parameters &params)
    {
        ImDrawList *const draw    = ImGui::GetWindowDrawList();
        const float diagramHeight = static_cast<float>(GD_bb(params.graph).UR.y);

        // -----

        for (Agnode_t *node = agfstnode(params.graph); node; node = agnxtnode(params.graph, node))
        {
            // Convert from inch to typographic points.
            Vec2 halfSize(
                static_cast<float>(ND_width(node)) * 0.5f * PPI, static_cast<float>(ND_height(node)) * 0.5f * PPI);

            // Coordinate of the node centre.
            const pointf &centre = ND_coord(node);

            const Color borderColor = ExtractColor(node, "color", IM_COL32(0, 0, 0, 255));
            const Color fillColor   = ExtractColor(node, "fillcolor", IM_COL32(255, 255, 255, 255));

            const shape_desc *shape = ND_shape(node);
            if (std::strcmp(shape->name, "diamond") == 0)
            {
                const Vec2 top    = ConvertPoint(params, { centre.x, centre.y + halfSize.y });
                const Vec2 bottom = ConvertPoint(params, { centre.x, centre.y - halfSize.y });
                const Vec2 left   = ConvertPoint(params, { centre.x - halfSize.x, centre.y });
                const Vec2 right  = ConvertPoint(params, { centre.x + halfSize.x, centre.y });

                if (fillColor.isValid) draw->AddQuadFilled(top, right, bottom, left, fillColor.color);
                draw->AddQuad(top, right, bottom, left, borderColor.color);
            }
            else if (std::strcmp(shape->name, "ellipse") == 0 || std::strcmp(shape->name, "oval") == 0)
            {
                const Vec2 radius        = halfSize * PIXEL_PER_PPI * params.zoom;
                const Vec2 centreInPixel = ConvertPoint(params, centre);

                if (fillColor.isValid) draw->AddEllipseFilled(centreInPixel, radius, fillColor.color);
                draw->AddEllipse(centreInPixel, radius, borderColor.color);
            }
            else if (std::strcmp(shape->name, "circle") == 0)
            {
                // Graphviz guarantee that the width is always equal to height for the circle shape.
                const float radius       = halfSize.x * PIXEL_PER_PPI * params.zoom;
                const Vec2 centreInPixel = ConvertPoint(params, centre);

                if (fillColor.isValid) draw->AddCircleFilled(centreInPixel, radius, fillColor.color);
                draw->AddCircle(centreInPixel, radius, borderColor.color);
            }
            // The Default shape is box, rect or rectangle.
            else
            {
                const Vec2 min = ConvertPoint(params, centre - halfSize);
                const Vec2 max = ConvertPoint(params, centre + halfSize);

                if (fillColor.isValid) draw->AddRectFilled(min, max, fillColor.color);
                draw->AddRect(min, max, borderColor.color);
            }

            // ----- Draw the label

            {
                const textlabel_t *const label = ND_label(node);
                DrawLabel(params, label, IM_COL32(0, 0, 0, 255), &centre);
            }

            // -----

            DrawArcs(params, node);
        }
    }

    /// @brief Draw all the arcs of a node.
    /// @param params The internal state and parameters to use.
    /// @param node The Graphiviz node.
    static void DrawArcs(const Parameters &params, Agnode_t *const node)
    {
        ImDrawList *const draw    = ImGui::GetWindowDrawList();
        const float diagramHeight = static_cast<float>(GD_bb(params.graph).UR.y);

        for (Agedge_t *arc = agfstout(params.graph, node); arc; arc = agnxtout(params.graph, arc))
        {
            const splines *spline = ED_spl(arc);
            if (!spline) continue;

            const Color color = ExtractColor(arc, "color", IM_COL32(0, 0, 0, 255));

            for (size_t i = 0; i < spline->size; ++i)
            {
                const bezier &bezier = spline->list[i];

                // The control points of the cubic segments are grouped by 3, where the initial point is shared with the
                // final point of the previous segment.
                // For example, the sequence [p0, c1, c2, p1, c3, c4, p2] correspond to two segments.
                // The first is [p0, c1, c2, p1] and the second is [p1, c3, c4, p2].
                for (size_t j = 0; j + 3 < bezier.size; j += 3)
                {
                    const Vec2 p0 = ConvertPoint(params, bezier.list[j + 0]);
                    const Vec2 c1 = ConvertPoint(params, bezier.list[j + 1]);
                    const Vec2 c2 = ConvertPoint(params, bezier.list[j + 2]);
                    const Vec2 p1 = ConvertPoint(params, bezier.list[j + 3]);

                    draw->AddBezierCubic(p0, c1, c2, p1, color.color, 1.0f);
                }

                // Arrow tip at the arc begin.
                if (bezier.sflag)
                {
                    const Vec2 apex = ConvertPoint(params, bezier.sp);
                    const Vec2 from = ConvertPoint(params, bezier.list[1]);
                    DrawArrowTip(params, apex, from, color.color);
                }

                // Arrow tip at the arc end.
                if (bezier.eflag)
                {
                    const Vec2 apex = ConvertPoint(params, bezier.ep);
                    const Vec2 from = ConvertPoint(params, bezier.list[bezier.size - 1]);
                    DrawArrowTip(params, apex, from, color.color);
                }
            }

            // ----- Draw the label

            {
                const textlabel_t *const label = ED_label(arc);
                DrawLabel(params, label, IM_COL32(0, 0, 0, 255));
            }
        }
    }

    /// @brief Draw the tip of a arrow.
    /// @param params The internal state and parameters to use.
    /// @param apex The coordinate of the apex of the arrow tip. [pixel]
    /// @param from The coordinate of the arc point where the arrow tip is placed. [pixel]
    /// @param color The color of the arrow tip.
    static void DrawArrowTip(const Parameters &params, const Vec2 &apex, const Vec2 &from, const ImU32 color)
    {
        // Direction to the arrow tip.
        Vec2 direction(apex.x - from.x, apex.y - from.y);
        if (!direction.Normalize()) return;

        // Perpendicular unit vector.
        const Vec2 n(-direction.y, direction.x);

        static constexpr float TIP_LENGTH = 15.0f;             // [pixel]
        static constexpr float TIP_WIDTH  = TIP_LENGTH * 0.5f; // [pixel]

        // Centre point of the base of the triangle.
        const Vec2 base = apex - direction * TIP_LENGTH * params.zoom;

        const Vec2 v0 = apex;
        const Vec2 v1 = base + n * TIP_WIDTH * params.zoom;
        const Vec2 v2 = base - n * TIP_WIDTH * params.zoom;

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        draw->AddTriangleFilled(v0, v1, v2, color);
    }

    /// @brief Draw a Graphiviz label of a node or an arc or other.
    /// @param params The internal state and parameters to use.
    /// @param label The Graphviz label to draw.
    /// @param defaultColor The color to use if the label it self don't have one.
    /// @param position Optional coordinate of the label position, they are used when the label does not provide a
    ///                 position by itself (like the nodes labels for example). [pixel]
    static void DrawLabel(
        const Parameters &params,
        const textlabel_t *const label,
        const ImU32 defaultColor,
        const pointf *const position)
    {
        if (!label || !label->text || label->text[0] == '\0') return;
        if (!position && !label->set) return;

        const float diagramHeight = static_cast<float>(GD_bb(params.graph).UR.y);

        ImFont *const font   = ImGui::GetIO().Fonts->Fonts[0];
        const float fontSize = static_cast<float>(label->fontsize) * params.zoom;
        const Vec2 textSize  = font->CalcTextSizeA(fontSize, std::numeric_limits<float>::max(), -1.0f, label->text);

        const Color color = ExtractColor(label->fontcolor, defaultColor);

        Vec2 pos;

        if (label->set) pos = ConvertPoint(params, label->pos);
        else pos = ConvertPoint(params, *position);

        pos -= textSize / 2.0f;

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        draw->AddText(font, fontSize, pos, color.color, label->text);
    }

    /// @brief Converte un punto dal sistema di riferimento di Graphviz a pixel (sistema di riferimento di ImGui).
    /// @param point Il punto da convertire. [PPI]
    /// @param diagramHeight L'altezza totale del diagramma. [PPI]
    /// @param diagramPos La posizione del diagramma in pixel.
    /// @return Le coordinate del punto convertite in pixel e ribaltate sull'asse x (sotto-sopra).
    static Vec2 ConvertPoint(const Parameters &params, const Vec2 &point)
    {
        const float diagramHeight = static_cast<float>(GD_bb(params.graph).UR.y);

        // Ribalto il diagramma in orizzontale (intorno all'asse x).
        Vec2 p(point.x, diagramHeight - point.y);

        // Conversione da punti tipografici a pixel.
        p *= PIXEL_PER_PPI;

        // Fattore di scala del diagramma.
        p *= params.zoom;

        // Traslazione del punto rispetto alla posizione del diagramma sullo schermo.
        p += params.diagramPos;

        return p;
    }

    /// @brief Recupera ed estrae un colore in formato Graphviz da una proprietà di un oggetto di Graphviz e lo converte
    /// in formato ImGui.
    /// @param object L'oggetto di Graphviz.
    /// @param name Il nome della proprietà dell'oggetto.
    /// @param defaultColor Il colore in formato ImGui da restituire se l'estrazione fallisce.
    /// @return Il colore estratto con validità a True in caso di successo, il colore predefinito e la validità a False
    ///         in caso di fallimento.
    static Color ExtractColor(void *object, const char *name, const ImColor defaultColor)
    {
        const char *color = agget(object, const_cast<char *>(name));
        return ExtractColor(color, defaultColor);
    }

    /// @brief Data una stringa con un colore in formato Graphviz, ne estrae il colore e lo converte in formato ImGui.
    /// @param colore Il colore in formato Graphviz.
    /// @param defaultColor Il colore in formato ImGui da restituire se l'estrazione fallisce.
    /// @return Il colore estratto con validità a True in caso di successo, il colore predefinito e la validità a False
    ///         in caso di fallimento.
    static Color ExtractColor(const char *color, const ImColor defaultColor)
    {
        if (!color || color[0] == '\0') return { defaultColor, false };

        gvcolor_t coloreGV;
        if (colorxlate(color, &coloreGV, RGBA_BYTE) == COLOR_OK)
            return { IM_COL32(coloreGV.u.rgba[0], coloreGV.u.rgba[1], coloreGV.u.rgba[2], coloreGV.u.rgba[3]), true };
        else return { defaultColor, false };
    }
}
