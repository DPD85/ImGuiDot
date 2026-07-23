#include "ImGuiDot.h"

#include "ImGuiDot_Structs.h"

#include <algorithm>
#include <cgraph.h>
#include <colorprocs.h>
#include <cstdint>
#include <cstring>
#include <gvc.h>
#include <gvplugin.h>
#include <imgui.h>
#include <limits>

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

    // Arrowhead flag format in Graphviz (https://graphviz.org/doc/info/arrows.html).
    //
    // Up to 4 arrowhead types combined together, 1 byte per arrowhead type.
    // All bytes share the same format:
    //    RLIB TTTT
    // where
    //   - R = bit set if only the right half of the arrowhead should be drawn;
    //   - L = bit set if only the left half of the arrowhead should be drawn;
    //     I = bit set if the shape should be draw inverted (rotate of 180° around itself centre);
    //   - B = bit set if the outline of the arrowhead should be drawn;
    //   - T = 4-bit number indicating the arrowhead type;
    //   - if neither R nor L is set, the full arrowhead should be drawn.
    static const constexpr uint8_t ARROW_SHAPE_MASK      = 0x0F;
    static const constexpr uint8_t ARROW_OUTLINE_MASK    = 0x10;
    static const constexpr uint8_t ARROW_INVERT_MASK     = 0x20;
    static const constexpr uint8_t ARROW_HALF_LEFT_MASK  = 0x40;
    static const constexpr uint8_t ARROW_HALF_RIGHT_MASK = 0x80;

    namespace
    {
        enum class ArrowheadShapes : std::uint8_t
        {
            None    = 0,
            Normal  = 1, // Inv = inverted normal
            Crow    = 2, // vee = inverted crow
            Tee     = 3,
            Box     = 4,
            Diamond = 5,
            Dot     = 6,
            Curve   = 7, // icurve = inverted curve
            Gap     = 8, // What is this?
        };
    }

    // ----- Global variables to use the Graphviz plugins -----

    extern "C"
    {
        extern gvplugin_library_t gvplugin_dot_layout_LTX_library;
    }

    static constexpr lt_symlist_t gvPlugins[] = {
        { "gvplugin_dot_layout_LTX_library", &gvplugin_dot_layout_LTX_library }, { nullptr, nullptr }
    };

    // ----- Structure and data used to make Graphviz read from a string not null terminated -----

    namespace
    {
        struct MemoryData
        {
            const char *data;
            int len;
            int cur;
        };
    }

    static int MemoryReader(void *const chan, char *const buf, const int bufsize)
    {
        if (bufsize == 0) return 0;

        auto *memoryData = static_cast<MemoryData *>(chan);
        if (memoryData->cur >= memoryData->len) return 0;

        const int bytesToCopy = std::min(memoryData->len - memoryData->cur, bufsize);

        memcpy(buf, memoryData->data + memoryData->cur, bytesToCopy);

        memoryData->cur += bytesToCopy;

        return bytesToCopy;
    }

    // ----- -----

    /// @brief Graphviz context.
    static GVC_t *gvContext = nullptr;

    namespace
    {
        /// @brief Internal state and parameters shared between various functions.
        struct Parameters
        {
            Agraph_t *graph;
            float zoom;
            Vec2 diagramPos; // [pixel]
        };
    }

    // ----- -----

    static void DrawNodes(const Parameters &params);
    static void DrawArcs(const Parameters &params, Agnode_t *node);
    static void DrawArrowhead(
        const Parameters &params, const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadNormal(
        const Parameters &params, const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadBox(const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadTee(const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadDiamond(const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadDot(const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadCrow(
        const Parameters &params, const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawArrowheadCurve(const Vec2 &apex, const Vec2 &base, ImU32 colour, uint32_t flags);
    static void DrawLabel(
        const Parameters &params, const textlabel_t *label, ImU32 defaultColour, const pointf *position = nullptr);
    static Vec2 ConvertPoint(const Parameters &params, const Vec2 &point);
    static Colour ExtractColour(void *object, const char *name, ImColor defaultColour);
    static Colour ExtractColour(const char *colour, ImColor defaultColour);

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

    void Diagram(const std::string_view &code, const float zoom)
    {
        Diagram(code.data(), code.data() + code.size(), zoom);
    }

    void Update(DiagramState &diagram, const char *const code, const char *endCode)
    {
        // ----- Construct and layout of the diagram

        if (endCode == nullptr) endCode = code + std::strlen(code);

        MemoryData input{ /*.data =*/code, /*.len =*/static_cast<int>(endCode - code), /*.cur =*/0 };
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

    void Update(DiagramState &diagram, const std::string_view &code)
    {
        Update(diagram, code.data(), code.data() + code.size());
    }

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

        Parameters params{ /*.graph =*/diagram.graph, /*.zoom =*/zoom, /* .diagramPos =*/{} };

        // -----

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const Vec2 cursorPos   = ImGui::GetCursorScreenPos();
        // const Vec2 spaceAvail  = ImGui::GetContentRegionAvail();

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
            const Colour colour = ExtractColour(params.graph, "bgcolor", IM_COL32(255, 255, 255, 255));
            if (colour.isValid)
            {
                const Vec2 min = params.diagramPos;
                const Vec2 max = params.diagramPos + size;
                draw->AddRectFilled(min, max, colour.colour);
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
        ImDrawList *const draw = ImGui::GetWindowDrawList();

        // -----

        for (Agnode_t *node = agfstnode(params.graph); node; node = agnxtnode(params.graph, node))
        {
            const Colour borderColour = ExtractColour(node, "color", IM_COL32(0, 0, 0, 255));
            const Colour fillColour   = ExtractColour(node, "fillcolor", IM_COL32(255, 255, 255, 255));

            const shape_desc *shape = ND_shape(node);
            if (std::strcmp(shape->name, "ellipse") == 0 || std::strcmp(shape->name, "oval") == 0)
            {
                // Half of the shape size converted from inch to typographic points.
                const Vec2 halfSize(
                    static_cast<float>(ND_width(node)) * 0.5f * PPI, static_cast<float>(ND_height(node)) * 0.5f * PPI);
                const Vec2 radius = halfSize * PIXEL_PER_PPI * params.zoom;
                const Vec2 centre = ConvertPoint(params, ND_coord(node));

                if (fillColour.isValid) draw->AddEllipseFilled(centre, radius, fillColour.colour);
                draw->AddEllipse(centre, radius, borderColour.colour);
            }
            else if (std::strcmp(shape->name, "circle") == 0)
            {
                // Graphviz guarantee that the width is always equal to height for the circle shape.

                // Half of the shape width converted from inch to typographic points.
                const float halfWidth = static_cast<float>(ND_width(node)) * 0.5f * PPI;
                const float radius    = halfWidth * PIXEL_PER_PPI * params.zoom;
                const Vec2 centre     = ConvertPoint(params, ND_coord(node));

                if (fillColour.isValid) draw->AddCircleFilled(centre, radius, fillColour.colour);
                draw->AddCircle(centre, radius, borderColour.colour);
            }
            // Polygon shapes.
            else if (
                std::strcmp(shape->name, "box") || std::strcmp(shape->name, "polygon")
                || std::strcmp(shape->name, "triangle") || std::strcmp(shape->name, "diamond")
                || std::strcmp(shape->name, "trapezium") || std::strcmp(shape->name, "parallelogram")
                || std::strcmp(shape->name, "house") || std::strcmp(shape->name, "pentagon")
                || std::strcmp(shape->name, "hexgon") || std::strcmp(shape->name, "septagon")
                || std::strcmp(shape->name, "octagon") || std::strcmp(shape->name, "invtriangle")
                || std::strcmp(shape->name, "invtrapezium") || std::strcmp(shape->name, "invhouse")
                || std::strcmp(shape->name, "rect") || std::strcmp(shape->name, "rectangle")
                || std::strcmp(shape->name, "square") || std::strcmp(shape->name, "egg"))
            {
                const pointf &centre   = ND_coord(node);
                const auto *polygon    = static_cast<polygon_t *>(ND_shape_info(node));
                const pointf *vertices = polygon->vertices;

                const size_t MAX_VERTEXES = 120;
                // Vertexes of the shape converted in pixel.
                Vec2 verts[MAX_VERTEXES];

                // Note: the polygon shape can have any numbers of sides because the user can specify it from the code.
                if (polygon->sides > MAX_VERTEXES)
                {
                    // The shape have too much sides, skip it.

                    // std::cout << "Warning: The shape have too much sides (the maximums is " << MAX_VERTEXES
                    //           << "), skip it.\n";
                    continue;
                }

                for (size_t i = 0; i < polygon->sides; ++i)
                    verts[i] = ConvertPoint(params, centre + vertices[i]);

                if (fillColour.isValid) draw->AddConvexPolyFilled(verts, polygon->sides, fillColour.colour);
                draw->AddPolyline(verts, polygon->sides, borderColour.colour, ImDrawFlags_Closed, 1.0f);
            }
            // None shape or one of the not supported.
            //
            // The not supported shapes are:
            //  - point
            //  - cylinder (ok, ma diverso dalla documentazione)
            //  - Mdiamond (identico al diamond)
            //  - Msquare (identico al square)
            //  - Mcircle (sides == 2)
            //  - star (sbaglia perché il poligono è concavo e non convesso)
            //  - underline ( identico al rettangolo)
            //  - note ( identico al rettangolo)
            //  - tab ( identico al rettangolo)
            //  - folder ( identico al rettangolo)
            //  - box3d ( identico al rettangolo)
            //  - component ( identico al rettangolo)
            //  - promoter ( identico al rettangolo)
            //  - cds ( identico al rettangolo)
            //  - terminator ( identico al rettangolo)
            //  - utr ( identico al rettangolo)
            //  - primersite ( identico al rettangolo)
            //  - restrictionsite ( identico al rettangolo)
            //  - fivepoverhang ( identico al rettangolo)
            //  - threepoverhang ( identico al rettangolo)
            //  - noverhang ( identico al rettangolo)
            //  - assembly ( identico al rettangolo)
            //  - signature ( identico al rettangolo)
            //  - insulator ( identico al rettangolo)
            //  - ribosite ( identico al rettangolo)
            //  - rnastab ( identico al rettangolo)
            //  - proteasesite ( identico al rettangolo)
            //  - proteinstab ( identico al rettangolo)
            //  - rpromoter ( identico al rettangolo)
            //  - rarrow ( identico al rettangolo)
            //  - larrow ( identico al rettangolo)
            //  - lpromoter ( identico al rettangolo)
            //
            //  - because have peripheries == 0: plaintext, plain
            //  - because have peripheries > 1 : doublecircle, doubleoctagon, tripleoctagon
            //  - because have sides == 1      : doublecircle, Mcircle
            else
            {
                // Nothing to draw.
            }

            // ----- Draw the label

            {
                const pointf &centre           = ND_coord(node);
                const textlabel_t *const label = ND_label(node);
                DrawLabel(params, label, IM_COL32(0, 0, 0, 255), &centre);
            }

            // -----

            DrawArcs(params, node);
        }
    }

    /// @brief Draw all the arcs of a node.
    /// @param params The internal state and parameters to use.
    /// @param node The Graphviz node.
    static void DrawArcs(const Parameters &params, Agnode_t *const node)
    {
        ImDrawList *const draw = ImGui::GetWindowDrawList();

        for (Agedge_t *arc = agfstout(params.graph, node); arc; arc = agnxtout(params.graph, arc))
        {
            const splines *spline = ED_spl(arc);
            if (!spline) continue;

            const Colour colour = ExtractColour(arc, "color", IM_COL32(0, 0, 0, 255));

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

                    draw->AddBezierCubic(p0, c1, c2, p1, colour.colour, 1.0f);
                }

                // Arrowhead at the arc begin.
                if (bezier.sflag)
                {
                    const Vec2 apex = ConvertPoint(params, bezier.sp);
                    const Vec2 from = ConvertPoint(params, bezier.list[0]);
                    DrawArrowhead(params, apex, from, colour.colour, bezier.sflag);
                }

                // Arrowhead at the arc end.
                if (bezier.eflag)
                {
                    const Vec2 apex = ConvertPoint(params, bezier.ep);
                    const Vec2 from = ConvertPoint(params, bezier.list[bezier.size - 1]);
                    DrawArrowhead(params, apex, from, colour.colour, bezier.eflag);
                }
            }

            // ----- Draw the label

            {
                const textlabel_t *const label = ED_label(arc);
                DrawLabel(params, label, IM_COL32(0, 0, 0, 255));
            }
        }
    }

    /// @brief Draw a arrowhead.
    /// @param params The internal state and parameters to use.
    /// @param apex The coordinate of the apex of the arrowhead. [pixel]
    /// @param base The coordinate of the arc point where the arrowhead is placed, correspond to the base centre point
    ///             of the arrowhead. [pixel]
    /// @param colour The colour of the arrowhead.
    /// @param flags The Graphviz flags of the arrowhead.
    static void DrawArrowhead(
        const Parameters &params, const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        const auto shape = static_cast<ArrowheadShapes>(flags & ARROW_SHAPE_MASK);
        switch (shape)
        {
            case ArrowheadShapes::Box:
                DrawArrowheadBox(apex, base, colour, flags);
                break;
            case ArrowheadShapes::Tee:
                DrawArrowheadTee(apex, base, colour, flags);
                break;
            case ArrowheadShapes::Diamond:
                DrawArrowheadDiamond(apex, base, colour, flags);
                break;
            case ArrowheadShapes::Dot:
                DrawArrowheadDot(apex, base, colour, flags);
                break;
            case ArrowheadShapes::Crow:
                DrawArrowheadCrow(params, apex, base, colour, flags);
                break;
            case ArrowheadShapes::Curve:
                DrawArrowheadCurve(apex, base, colour, flags);
                break;
            case ArrowheadShapes::Normal:
            default:
                DrawArrowheadNormal(params, apex, base, colour, flags);
                break;
        }
    }

    /// @brief Draw a normal arrowhead (triangular shape).
    /// @copydetails DrawArrowhead
    static void DrawArrowheadNormal(
        const Parameters &params, const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        static constexpr float SHAPE_WIDTH = 5.0f; // [pixel]

        const bool drawInverted      = flags & ARROW_INVERT_MASK;
        const bool drawOnlyHalfLeft  = flags & ARROW_HALF_LEFT_MASK;
        const bool drawOnlyHalfRight = flags & ARROW_HALF_RIGHT_MASK;
        const bool drawOutline       = flags & ARROW_OUTLINE_MASK;

        // Apex and base points used for draw the shape.
        Vec2 apexDraw, baseDraw;
        if (drawInverted)
        {
            apexDraw = base;
            baseDraw = apex;
        }
        else
        {
            apexDraw = apex;
            baseDraw = base;
        }

        // Direction to the arrowhead tip.
        Vec2 direction = apexDraw - baseDraw;
        if (!direction.Normalize()) return;

        // Perpendicular unit vector scaled to include the proper length.
        const Vec2 n = Vec2(-direction.y, direction.x) * SHAPE_WIDTH * params.zoom;

        // Vertexes of the triangle.
        const Vec2 v0 = apexDraw;
        Vec2 v1;
        Vec2 v2;

        if (drawOnlyHalfRight)
        {
            v1 = baseDraw + n;
            v2 = baseDraw;
        }
        else if (drawOnlyHalfLeft)
        {
            v1 = baseDraw;
            v2 = baseDraw - n;
        }
        else
        {
            v1 = baseDraw + n;
            v2 = baseDraw - n;
        }

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        if (drawOutline) draw->AddTriangle(v0, v1, v2, colour);
        else draw->AddTriangleFilled(v0, v1, v2, colour);
    }

    /// @brief Draw a box arrowhead (box shape).
    /// @copydetails DrawArrowhead
    static void DrawArrowheadBox(const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        const bool drawOnlyHalfLeft  = flags & ARROW_HALF_LEFT_MASK;
        const bool drawOnlyHalfRight = flags & ARROW_HALF_RIGHT_MASK;
        const bool drawOutline       = flags & ARROW_OUTLINE_MASK;

        // Half of the direction to the arrowhead tip.
        const Vec2 direction = (apex - base) / 2.0f;
        // Perpendicular unit vector.
        const Vec2 n(-direction.y, direction.x);

        // Vertexes of the box.
        Vec2 v0, v1, v2, v3;

        if (drawOnlyHalfRight)
        {
            v0 = apex;
            v1 = apex + n;
            v2 = base + n;
            v3 = base;
        }
        else if (drawOnlyHalfLeft)
        {
            v0 = apex - n;
            v1 = apex;
            v2 = base;
            v3 = base - n;
        }
        else
        {
            v0 = apex - n;
            v1 = apex + n;
            v2 = base + n;
            v3 = base - n;
        }

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        if (drawOutline) draw->AddQuad(v0, v1, v2, v3, colour);
        else draw->AddQuadFilled(v0, v1, v2, v3, colour);
    }

    /// @brief Draw a tee arrowhead (rectangle shape).
    /// @copydetails DrawArrowhead
    static void DrawArrowheadTee(const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        const bool drawOnlyHalfLeft  = flags & ARROW_HALF_LEFT_MASK;
        const bool drawOnlyHalfRight = flags & ARROW_HALF_RIGHT_MASK;

        // Direction to the arrowhead tip.
        const Vec2 direction = (apex - base);
        // Perpendicular unit vector scaled to the proper width.
        const Vec2 n = Vec2(-direction.y, direction.x);

        // Centre point at the top of the rectangle.
        const Vec2 a = apex;
        // Centre point at the bottom of the rectangle.
        const Vec2 b = base + direction / 2.0f;

        // Vertexes of the rectangle.
        Vec2 v0, v1, v2, v3;

        if (drawOnlyHalfRight)
        {
            v0 = a;
            v1 = a + n;
            v2 = b + n;
            v3 = b;
        }
        else if (drawOnlyHalfLeft)
        {
            v0 = a - n;
            v1 = a;
            v2 = b;
            v3 = b - n;
        }
        else
        {
            v0 = a - n;
            v1 = a + n;
            v2 = b + n;
            v3 = b - n;
        }

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        draw->AddQuadFilled(v0, v1, v2, v3, colour);

        draw->PathLineTo(base);
        draw->PathLineTo(b);
        draw->PathStroke(colour);
    }

    /// @brief Draw a diamond arrowhead (rhombus shape).
    /// @copydetails DrawArrowhead
    static void DrawArrowheadDiamond(const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        const bool drawOnlyHalfLeft  = flags & ARROW_HALF_LEFT_MASK;
        const bool drawOnlyHalfRight = flags & ARROW_HALF_RIGHT_MASK;
        const bool drawOutline       = flags & ARROW_OUTLINE_MASK;

        // Half of the direction to the arrowhead tip.
        const Vec2 direction = (apex - base) / 2.0f;
        // Perpendicular unit vector scaled to include the proper length.
        const Vec2 n = Vec2(-direction.y, direction.x) * 0.6f;

        ImDrawList *const draw = ImGui::GetWindowDrawList();

        if (drawOnlyHalfRight)
        {
            // Vertexes of the triangle.
            const Vec2 v0 = apex;
            const Vec2 v1 = apex + n - direction;
            const Vec2 v2 = base;

            if (drawOutline) draw->AddTriangle(v0, v1, v2, colour);
            else draw->AddTriangleFilled(v0, v1, v2, colour);
        }
        else if (drawOnlyHalfLeft)
        {
            // Vertexes of the triangle.
            const Vec2 v0 = apex;
            const Vec2 v1 = base;
            const Vec2 v2 = base - n + direction;

            if (drawOutline) draw->AddTriangle(v0, v1, v2, colour);
            else draw->AddTriangleFilled(v0, v1, v2, colour);
        }
        else
        {
            // Vertexes of the rhombus.
            const Vec2 v0 = apex;
            const Vec2 v1 = apex + n - direction;
            const Vec2 v2 = base;
            const Vec2 v3 = base - n + direction;

            if (drawOutline) draw->AddQuad(v0, v1, v2, v3, colour);
            else draw->AddQuadFilled(v0, v1, v2, v3, colour);
        }
    }

    /// @brief Draw a dot arrowhead (circle shape).
    /// @copydetails DrawArrowhead
    static void DrawArrowheadDot(const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        const bool drawOutline = flags & ARROW_OUTLINE_MASK;

        const Vec2 centre  = (apex + base) / 2.0f;
        const float radius = (apex - base).Length() / 2.0f;

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        if (drawOutline) draw->AddCircle(centre, radius, colour);
        else draw->AddCircleFilled(centre, radius, colour);
    }

    /// @brief Draw a crow arrowhead.
    /// @copydetails DrawArrowhead
    static void DrawArrowheadCrow(
        const Parameters &params, const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        static constexpr float SHAPE_WIDTH = 5.0f; // [pixel]

        const bool drawInverted      = flags & ARROW_INVERT_MASK;
        const bool drawOnlyHalfLeft  = flags & ARROW_HALF_LEFT_MASK;
        const bool drawOnlyHalfRight = flags & ARROW_HALF_RIGHT_MASK;

        // Apex and base points used for draw the shape.
        Vec2 apexDraw, baseDraw;

        // The crow arrowhead are considered not inverted when point to the base instead of apex.
        if (drawInverted)
        {
            apexDraw = apex;
            baseDraw = base;
        }
        else
        {
            apexDraw = base;
            baseDraw = apex;
        }

        // Direction to the arrowhead tip scaled to the half of the shape height.
        const Vec2 direction = (apexDraw - baseDraw) / 2.0f;

        // Perpendicular unit vector scaled to include the proper length.
        Vec2 n(-direction.y, direction.x);
        if (!n.Normalize()) return;
        n *= SHAPE_WIDTH * params.zoom;

        // The shape are draw as two triangles, one is the half left the other the half right.

        const Vec2 v0 = apexDraw;
        const Vec2 v1 = baseDraw + n;
        const Vec2 v2 = baseDraw - n;
        // Vertex in the middle of the two half.
        const Vec2 v3 = baseDraw + direction;

        ImDrawList *const draw = ImGui::GetWindowDrawList();

        if (!drawOnlyHalfRight) draw->AddTriangleFilled(v0, v1, v3, colour);
        if (!drawOnlyHalfLeft) draw->AddTriangleFilled(v0, v2, v3, colour);

        draw->PathLineTo(v3);
        draw->PathLineTo(baseDraw);
        draw->PathStroke(colour);
    }

    /// @brief Draw a curve arrowhead (half circle shape).
    /// @copydetails DrawArrowhead
    static void DrawArrowheadCurve(const Vec2 &apex, const Vec2 &base, const ImU32 colour, const uint32_t flags)
    {
        const constexpr float PI = 3.141592f;

        const bool drawInverted      = flags & ARROW_INVERT_MASK;
        const bool drawOnlyHalfLeft  = flags & ARROW_HALF_LEFT_MASK;
        const bool drawOnlyHalfRight = flags & ARROW_HALF_RIGHT_MASK;

        const Vec2 centre  = (apex + base) / 2.0f;
        const float radius = (apex - base).Length() / 2.0f;

        const float angleOffset = drawInverted ? -PI / 2.0f : PI / 2.0f;
        const float angleBase   = atan2f(base.y - centre.y, base.x - centre.x) + angleOffset;

        // Angles where the semicircle begin and end.
        float angleStart;
        float angleEnd;

        if (drawOnlyHalfRight)
        {
            angleStart = angleBase + PI / 2.0f;
            angleEnd   = angleBase + PI;
        }
        else if (drawOnlyHalfLeft)
        {
            angleStart = angleBase;
            angleEnd   = angleBase + PI / 2.0f;
        }
        else
        {
            angleStart = angleBase;
            angleEnd   = angleBase + PI;
        }

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        draw->PathArcTo(centre, radius, angleStart, angleEnd);
        draw->PathStroke(colour);

        draw->PathLineTo(apex);
        draw->PathLineTo(base);
        draw->PathStroke(colour);
    }

    /// @brief Draw a Graphiviz label of a node or an arc or other.
    /// @param params The internal state and parameters to use.
    /// @param label The Graphviz label to draw.
    /// @param defaultColour The colour to use if the label it self don't have one.
    /// @param position Optional coordinate of the label position, they are used when the label does not provide a
    ///                 position by itself (like the nodes labels for example). [pixel]
    static void DrawLabel(
        const Parameters &params,
        const textlabel_t *const label,
        const ImU32 defaultColour,
        const pointf *const position)
    {
        if (!label || !label->text || label->text[0] == '\0') return;
        if (!position && !label->set) return;

        ImFont *const font   = ImGui::GetIO().Fonts->Fonts[0];
        const float fontSize = static_cast<float>(label->fontsize) * params.zoom;
        const Vec2 textSize  = font->CalcTextSizeA(fontSize, std::numeric_limits<float>::max(), -1.0f, label->text);

        const Colour colour = ExtractColour(label->fontcolor, defaultColour);

        Vec2 pos;

        if (label->set) pos = ConvertPoint(params, label->pos);
        else pos = ConvertPoint(params, *position);

        pos -= textSize / 2.0f;

        ImDrawList *const draw = ImGui::GetWindowDrawList();
        draw->AddText(font, fontSize, pos, colour.colour, label->text);
    }

    /// @brief Converts a point from Graphviz's coordinate system to pixels (ImGui's coordinate system).
    /// @param params The internal state and parameters to use.
    /// @param point The point to convert. [PPI]
    /// @return The point's coordinates converted to pixels and flipped on the x-axis (upside-down).
    static Vec2 ConvertPoint(const Parameters &params, const Vec2 &point)
    {
        const auto diagramHeight = static_cast<float>(GD_bb(params.graph).UR.y);

        // Flip the diagram vertically (around the x-axis).
        Vec2 p(point.x, diagramHeight - point.y);

        // Conversion from typographic points to pixels.
        p *= PIXEL_PER_PPI;

        // Diagram scale factor.
        p *= params.zoom;

        // Translate the point relative to the diagram's position on screen.
        p += params.diagramPos;

        return p;
    }

    /// @brief Retrieves and extracts a colour in Graphviz format from a Graphviz object property and converts it to
    ///        ImGui format.
    /// @param object The Graphviz object.
    /// @param name The name of the object's property.
    /// @param defaultColour The colour in ImGui format to return if extraction fails.
    /// @return The extracted colour with validity set to True on success, or the default colour with validity set to
    ///         False on failure.
    static Colour ExtractColour(void *object, const char *name, const ImColor defaultColour)
    {
        const char *colour = agget(object, const_cast<char *>(name));
        return ExtractColour(colour, defaultColour);
    }

    /// @brief Given a string containing a colour in Graphviz format, extracts the colour and converts it to ImGui
    ///        format.
    /// @param colour The colour in Graphviz format.
    /// @param defaultColour The colour in ImGui format to return if extraction fails.
    /// @return The extracted colour with validity set to True on success, or the default colour with validity set to
    ///         False on failure.
    static Colour ExtractColour(const char *colour, const ImColor defaultColour)
    {
        if (!colour || colour[0] == '\0') return { defaultColour, false };

        gvcolor_t coloreGV;
        if (colorxlate(colour, &coloreGV, RGBA_BYTE) == COLOR_OK)
            return { IM_COL32(coloreGV.u.rgba[0], coloreGV.u.rgba[1], coloreGV.u.rgba[2], coloreGV.u.rgba[3]), true };

        return { defaultColour, false };
    }
}
