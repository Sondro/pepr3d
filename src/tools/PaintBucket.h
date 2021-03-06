#pragma once
#include <optional>
#include "commands/CommandManager.h"
#include "geometry/Geometry.h"
#include "tools/Tool.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/SidePane.h"

namespace pepr3d {

/// Tool used for coloring whole regions with a single click
class PaintBucket : public Tool {
   public:
    explicit PaintBucket(MainApplication& app) : mApplication(app) {}

    virtual std::string getName() const override {
        return "Paint Bucket";
    }

    virtual std::string getDescription() const override {
        return "Color whole regions with a single click.";
    }

    virtual std::optional<Hotkey> getHotkey(const Hotkeys& hotkeys) const override {
        return hotkeys.findHotkey(HotkeyAction::SelectPaintBucket);
    }

    virtual std::string getIcon() const override {
        return ICON_MD_FORMAT_COLOR_FILL;
    }

    virtual bool isEnabled() const override {
        return mGeometryCorrect;
    }

    virtual void drawToSidePane(SidePane& sidePane) override;
    virtual void drawToModelView(ModelView& modelView) override;
    virtual void onModelViewMouseDown(ModelView& modelView, ci::app::MouseEvent event) override;
    virtual void onModelViewMouseDrag(ModelView& modelView, ci::app::MouseEvent event) override;
    virtual void onModelViewMouseMove(ModelView& modelView, ci::app::MouseEvent event) override;
    virtual void onNewGeometryLoaded(ModelView& modelView) override;
    virtual void onToolSelect(ModelView& modelView) override;

    enum NormalAngleCompare { NEIGHBOURS = 1, ABSOLUTE = 2 };

   private:
    MainApplication& mApplication;
    bool mStopOnNormal = false;
    int mStopOnNormalDegrees = 30;
    bool mStopOnColor = true;
    bool mDoNotStop = false;
    bool mShouldPaintWhileDrag = true;
    bool mDragging = false;
    bool mGeometryCorrect = true;
    NormalAngleCompare mNormalCompare = NormalAngleCompare::NEIGHBOURS;
    glm::ivec2 mLastMousePos;

    /// A paint bucket criterion that never stops, used for coloring the whole model
    struct DoNotStop {
        const Geometry* geo;

        DoNotStop(const Geometry* g) : geo(g) {}

        bool operator()(const DetailedTriangleId a, const DetailedTriangleId b) const {
            return true;
        }
    };

    /// A paint bucket criterion that stops on a different color
    struct ColorStopping {
        const Geometry* geo;

        ColorStopping(const Geometry* g) : geo(g) {}

        bool operator()(const DetailedTriangleId a, const DetailedTriangleId b) const {
            if(geo->getTriangle(a).getColor() == geo->getTriangle(b).getColor()) {
                return true;
            } else {
                return false;
            }
        }
    };

    /// A paint bucket criterion that stops when an angle between normals is too high
    struct NormalStopping {
        const Geometry* geo;
        const double threshold;
        const glm::vec3 startNormal;
        NormalAngleCompare angleCompare;

        NormalStopping(const Geometry* g, const double thresh, const glm::vec3 normal,
                       const NormalAngleCompare angleCmp)
            : geo(g), threshold(thresh), startNormal(normal), angleCompare(angleCmp) {}

        bool operator()(const DetailedTriangleId a, const DetailedTriangleId b) const {
            if(a.getBaseId() == b.getBaseId()) {
                return true;  // Details of the same base have the same normal
            }

            double cosAngle = 0.0;
            if(angleCompare == NormalAngleCompare::ABSOLUTE) {
                const auto& newNormal = geo->getTriangle(a).getNormal();
                cosAngle = glm::dot(glm::normalize(newNormal), glm::normalize(startNormal));
            } else if(angleCompare == NormalAngleCompare::NEIGHBOURS) {
                const auto& newNormal1 = geo->getTriangle(a).getNormal();
                const auto& newNormal2 = geo->getTriangle(b).getNormal();
                cosAngle = glm::dot(glm::normalize(newNormal1), glm::normalize(newNormal2));
            } else {
                assert(false);
            }

            if(cosAngle < threshold) {
                return false;
            } else {
                return true;
            }
        }
    };
};
}  // namespace pepr3d
