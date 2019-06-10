// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "Viewer.h"

#include <Corrade/Utility/Arguments.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Renderer.h>
#include <sophus/so3.hpp>

#include "Drawable.h"
#include "esp/io/io.h"

using namespace Magnum;
using namespace Math::Literals;
using namespace Corrade;

constexpr float moveSensitivity = 0.1f;
constexpr float lookSensitivity = 11.25f;
constexpr float cameraHeight = 1.5f;

namespace esp {
namespace gfx {

Viewer::Viewer(const Arguments& arguments)
    : Platform::Application{arguments,
                            Configuration{}.setTitle("Viewer").setWindowFlags(
                                Configuration::WindowFlag::Resizable),
                            GLConfiguration{}.setColorBufferSize(
                                Vector4i(8, 8, 8, 8))},
      pathfinder_(nav::PathFinder::create()),
      controls_(),
      previousPosition_() {
  Utility::Arguments args;
  args.addArgument("file")
      .setHelp("file", "file to load")
      .addBooleanOption("action-path")
      .setHelp("action-path",
               "Provides actions along the action space shortest path to a "
               "random goal")
      .addBooleanOption("enable-physics")
      //.setHelp()
      .addSkippedPrefix("magnum", "engine-specific options")
      .setGlobalHelp("Displays a 3D scene file provided on command line")
      .parse(arguments.argc, arguments.argv);

  const auto viewportSize = GL::defaultFramebuffer.viewport().size();
  computeActionPath_ = args.isSet("action-path");
  enablePhysics_ = args.isSet("enable-physics");

  // Setup renderer and shader defaults
  GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
  GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);

  int sceneID = sceneManager_.initSceneGraph();
  sceneID_.push_back(sceneID);
  auto& sceneGraph = sceneManager_.getSceneGraph(sceneID);
  auto& rootNode = sceneGraph.getRootNode();

  navSceneNode_ = &rootNode.createChild();
  objNode_ = &navSceneNode_->createChild();

  if (enablePhysics_) {
    // ======= Init timestep, physics starts =======
    physicsManager_.initPhysics();
  }

  auto& drawables = sceneGraph.getDrawables();
  const std::string& file = args.value("file");
  const assets::AssetInfo info = assets::AssetInfo::fromPath(file);
  LOG(INFO) << "Nav scene node (before) " << navSceneNode_;  
  if (!resourceManager_.loadPhysicalScene(info, physicsManager_, navSceneNode_, enablePhysics_, &drawables)) {
    LOG(ERROR) << "cannot load " << file;
    std::exit(0);
  }

  // Set up physics
  LOG(INFO) << "Nav scene node (done) " << navSceneNode_;
  std::string object_file ("./data/objects/textured.glb");
  //std::string object_file ("./data/objects/cube.glb");
  assets::AssetInfo object_info = assets::AssetInfo::fromPath(object_file);
  LOG(INFO) << "Loading object from " << object_file;
  // Root node
  //bool objectLoaded_ = resourceManager_.loadObject(object_info, physicsManager_, navSceneNode_, objNode_, enablePhysics_, &drawables);
  bool objectLoaded_ = resourceManager_.loadObject(object_info, physicsManager_, objNode_, enablePhysics_, &drawables);
  
  if (objectLoaded_) {
    if (enablePhysics_) {

      LOG(INFO) << "Loaded";

      // Loop at 60 Hz max
      setSwapInterval(1);
      physicsManager_.debugSceneGraph(&rootNode);
    }
  } else {
    LOG(ERROR) << "cannot load " << object_file;
    std::exit(0);
  }


  // Set up camera
  renderCamera_ = &sceneGraph.getDefaultRenderCamera();
  agentBodyNode_ = &rootNode.createChild();
  cameraNode_ = &agentBodyNode_->createChild();

  // TODO (JH) hacky look back
  //cameraNode_->rotate(Math::piHalf, vec3f(0, 0, 1));
  //cameraNode_->translate(vec3f(0.0, cameraHeight, 0.0));
  //cameraNode_->translate(vec3f(0.0, 0.0, cameraHeight));
  //cameraNode_->translate(vec3f(8.0f, cameraHeight, -8.0f));
  Magnum::Matrix4 oldT = cameraNode_->MagnumObject::absoluteTransformation();
  LOG(INFO) << "Camera old transformation " << Eigen::Map<mat4f>(oldT.data());
  Vector3 old_pos = oldT.transformPoint({0.0f, 0.0f, -1.0f});
  LOG(INFO) << "Object old position " << Eigen::Map<vec3f>(old_pos.data());
  

  //agentBodyNode_->rotate(3.14f, vec3f(0, 1, 0));
  //cameraNode_->rotate(3.14f, vec3f(0, 1, 0));       // (JH) strange this is not working
  agentBodyNode_->translate(vec3f(0, 1.5f, 10.0f));


  float hfov = 90.0f;
  int width = viewportSize[0];
  int height = viewportSize[1];
  const float aspectRatio = static_cast<float>(width) / height;
  float znear = 0.01f;
  float zfar = 1000.0f;
  renderCamera_->setProjectionMatrix(width, height, znear, zfar, hfov);


  // (JH) hacky way to set orientation
  //renderCamera_->getSceneNode()->MagnumObject::rotate(Math::Rad<float>{3.14f},
  //                                                    Vector3(0, 0, 1));



  // Load navmesh if available
  const std::string navmeshFilename = io::changeExtension(file, ".navmesh");
  if (io::exists(navmeshFilename)) {
    LOG(INFO) << "Loading navmesh from " << navmeshFilename;
    pathfinder_->loadNavMesh(navmeshFilename);
    LOG(INFO) << "Loaded.";
  }

  // Messing around with agent location and finding object initial position
  LOG(INFO) << "Agent position " << agentBodyNode_->getAbsolutePosition();
  LOG(INFO) << "Camera position " << cameraNode_->getAbsolutePosition();
  LOG(INFO) << "Scene position" << navSceneNode_->getAbsolutePosition();
  //Magnum::Matrix4 absT = cameraNode_->MagnumObject::absoluteTransformation();
  //Magnum::Matrix4 T = cameraNode_->MagnumObject::transformationMatrix();    // Relative to agent bodynode
  Magnum::Matrix4 absT = agentBodyNode_->MagnumObject::absoluteTransformation();
  Magnum::Matrix4 T = agentBodyNode_->MagnumObject::transformationMatrix();    // Relative to agent bodynode
  //auto transformation = Matrix4(cameraNode_->getAbsoluteTransformation());
  //Vector3 new_pos = absT.transformPoint({0.0f, 0.0f, -1.0f});
  Vector3 new_pos = T.transformPoint({0.0f, 0.0f, -3.0f});
  
  //Vector3 new_pos = Vector3(0.0f, -10.0f, 3.0f);
  
  LOG(INFO) << "Camera position " << T.translation().x() << " " << T.translation().y() << " " << T.translation().z();
  //Vector3 new_pos = T.translation() + delta;
  //Vector3 new_pos = Vector3(cameraNode_->getAbsolutePosition()) + delta;
  LOG(INFO) << "Object new position " << new_pos.x() << " " << new_pos.y() << " " << new_pos.z();
  LOG(INFO) << "Camera transformation" << Eigen::Map<mat4f>(T.data());
  LOG(INFO) << "Camera abs transformation" << Eigen::Map<mat4f>(absT.data());

  objNode_->setTranslation(vec3f(new_pos.x(), new_pos.y(), new_pos.z()));
  static_cast<physics::BulletRigidObject*>(objNode_)->syncPose();
  static_cast<physics::BulletRigidObject*>(navSceneNode_)->syncPose();

  Magnum::Matrix4 new_objT = objNode_->MagnumObject::transformationMatrix();
  LOG(INFO) << "Object updated position " << Eigen::Map<vec3f>(new_objT.translation().data());
  
  // Connect controls to navmesh if loaded
  /*if (pathfinder_->isLoaded()) {
    controls_.setMoveFilterFunction([&](const vec3f& start, const vec3f& end) {
      vec3f currentPosition = pathfinder_->tryStep(start, end);
      LOG(INFO) << "position=" << currentPosition.transpose() << " rotation="
                << agentBodyNode_->getRotation().coeffs().transpose();
      LOG(INFO) << "Distance to closest obstacle: "
                << pathfinder_->distanceToClosestObstacle(currentPosition);

      if (computeActionPath_) {
        nav::ActionSpaceShortestPath spath;
        spath.requestedEnd = nav::ActionSpacePathLocation::create(
            goalPos_, goalHeading_.coeffs());

        spath.requestedStart = nav::ActionSpacePathLocation::create(
            currentPosition, agentBodyNode_->getRotation().coeffs());

        if (!actPathfinder_->findPath(spath)) {
          LOG(INFO) << "Could not find a path :(";
        } else if (spath.actions.size() == 0) {
          LOG(INFO) << "You made it!";
        } else {
          LOG(INFO) << "next action=" << spath.actions[0];
          LOG(INFO) << "actions left=" << spath.actions.size();
          LOG(INFO) << "geo dist=" << spath.geodesicDistance;
          LOG(INFO) << "predicted next pos=" << spath.points[1].transpose();
          LOG(INFO) << "predicted next rotation="
                    << spath.rotations[1].transpose();
        }
      }

      return currentPosition;
    });

    const vec3f position = pathfinder_->getRandomNavigablePoint();
    agentBodyNode_->setTranslation(position);

    if (computeActionPath_) {
      {
        nav::ShortestPath path_;
        do {
          goalPos_ = pathfinder_->getRandomNavigablePoint();
          goalPos_[1] = position[1];
          path_.requestedStart = position;
          path_.requestedEnd = goalPos_;
          pathfinder_->findPath(path_);
        } while ((path_.geodesicDistance < 1.0) ||
                 (path_.geodesicDistance > 2.0));
      }

      goalHeading_ =
          Sophus::SO3f::exp(M_PI / 4.0 * vec3f::UnitY()).unit_quaternion();

      agent::AgentConfiguration agentCfg;
      agentCfg.actionSpace["moveForward"]->actuation["amount"] =
          moveSensitivity;
      agentCfg.actionSpace["lookLeft"]->actuation["amount"] = lookSensitivity;
      agentCfg.actionSpace["lookRight"]->actuation["amount"] = lookSensitivity;
      actPathfinder_ = nav::ActionSpacePathFinder::create_unique(
          pathfinder_, agentCfg, controls_, agentBodyNode_->getRotation());
    }
  }*/

  LOG(INFO) << "Viewer initialization is done. ";
  renderCamera_->setTransformation(cameraNode_->getAbsoluteTransformation());
}  // namespace gfx

Vector3 positionOnSphere(Magnum::SceneGraph::Camera3D& camera,
                         const Vector2i& position) {
  const Vector2 positionNormalized =
      Vector2{position} / Vector2{camera.viewport()} - Vector2{0.5f};
  const Float length = positionNormalized.length();
  const Vector3 result(length > 1.0f
                           ? Vector3(positionNormalized, 0.0f)
                           : Vector3(positionNormalized, 1.0f - length));
  return (result * Vector3::yScale(-1.0f)).normalized();
}

void Viewer::drawEvent() {
  GL::defaultFramebuffer.clear(GL::FramebufferClear::Color |
                               GL::FramebufferClear::Depth);
  if (sceneID_.size() <= 0)
    return;

  physicsManager_.stepPhysics();

  int DEFAULT_SCENE = 0;
  int sceneID = sceneID_[DEFAULT_SCENE];
  auto& sceneGraph = sceneManager_.getSceneGraph(sceneID);
  renderCamera_->getMagnumCamera().draw(sceneGraph.getDrawables());
  swapBuffers();
  physicsManager_.nextFrame();
  //LOG(INFO) << "Current position " << objNode_->getAbsolutePosition();
  redraw();
}

void Viewer::viewportEvent(ViewportEvent& event) {
  GL::defaultFramebuffer.setViewport({{}, framebufferSize()});
  renderCamera_->getMagnumCamera().setViewport(event.windowSize());
}

void Viewer::mousePressEvent(MouseEvent& event) {
  if (event.button() == MouseEvent::Button::Left)
    previousPosition_ =
        positionOnSphere(renderCamera_->getMagnumCamera(), event.position());
}

void Viewer::mouseReleaseEvent(MouseEvent& event) {
  if (event.button() == MouseEvent::Button::Left)
    previousPosition_ = Vector3();
}

void Viewer::mouseScrollEvent(MouseScrollEvent& event) {
  if (!event.offset().y()) {
    return;
  }

  /* Distance to origin */
  const float distance = renderCamera_->getTransformation().col(3).z();

  /* Move 15% of the distance back or forward */
  renderCamera_->translateLocal(vec3f(
      0, 0, distance * (1.0f - (event.offset().y() > 0 ? 1 / 0.85f : 0.85f))));

  redraw();
}

void Viewer::mouseMoveEvent(MouseMoveEvent& event) {
  if (!(event.buttons() & MouseMoveEvent::Button::Left)) {
    return;
  }

  const Vector3 currentPosition =
      positionOnSphere(renderCamera_->getMagnumCamera(), event.position());
  const Vector3 axis = Math::cross(previousPosition_, currentPosition);

  if (previousPosition_.length() < 0.001f || axis.length() < 0.001f) {
    return;
  }
  const auto angle = Math::angle(previousPosition_, currentPosition);
  renderCamera_->getSceneNode()->MagnumObject::rotate(-angle,
                                                      axis.normalized());
  previousPosition_ = currentPosition;

  redraw();
}

void Viewer::keyPressEvent(KeyEvent& event) {
  const auto key = event.key();
  switch (key) {
    case KeyEvent::Key::Esc:
      std::exit(0);
      break;
    case KeyEvent::Key::Left:
      controls_(*agentBodyNode_, "lookLeft", lookSensitivity);
      break;
    case KeyEvent::Key::Right:
      controls_(*agentBodyNode_, "lookRight", lookSensitivity);
      break;
    case KeyEvent::Key::Up:
      controls_(*cameraNode_, "lookUp", lookSensitivity, false);
      break;
    case KeyEvent::Key::Down:
      controls_(*cameraNode_, "lookDown", lookSensitivity, false);
      break;
    case KeyEvent::Key::Nine: {
      const vec3f position = pathfinder_->getRandomNavigablePoint();
      agentBodyNode_->setTranslation(position);
    } break;
    case KeyEvent::Key::A:
      //controls_(*agentBodyNode_, "moveLeft", moveSensitivity);
      controls_(*objNode_, "moveLeft", moveSensitivity);
      break;
    case KeyEvent::Key::D:
      //controls_(*agentBodyNode_, "moveRight", moveSensitivity);
      controls_(*objNode_, "moveRight", moveSensitivity);
      break;
    case KeyEvent::Key::S:
      //controls_(*agentBodyNode_, "moveBackward", moveSensitivity);
      controls_(*objNode_, "moveBackward", moveSensitivity);
      break;
    case KeyEvent::Key::W:
      //controls_(*agentBodyNode_, "moveForward", moveSensitivity);
      controls_(*objNode_, "moveForward", moveSensitivity);
      break;
    case KeyEvent::Key::X:
      //controls_(*cameraNode_, "moveDown", moveSensitivity, false);
      controls_(*objNode_, "moveDown", moveSensitivity, false);
      break;
    case KeyEvent::Key::Z:
      //controls_(*cameraNode_, "moveUp", moveSensitivity, false);
      controls_(*objNode_, "moveUp", moveSensitivity, false);
      break;
    default:
      break;
  }
  renderCamera_->setTransformation(cameraNode_->getAbsoluteTransformation());
  redraw();
}

}  // namespace gfx
}  // namespace esp
