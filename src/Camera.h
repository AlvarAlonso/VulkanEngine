#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

enum Camera_Type {
    PERSPECTIVE = 0,
    ORTHOGRAPHIC
};

const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 0.1f;
const float SENSITIVITY = 0.1f;

class Camera
{
public:

    Camera_Type _type;

    Camera();

    glm::vec3 _position;
    glm::vec3 _direction;
    glm::vec3 _up_vec;
    glm::vec3 _right_vec;

    float _yaw;
    float _pitch;
    float _speed;
    float _sensitivity;

    //properties of the projection of the camera
    float _fov;			
    float _aspect;		
    float _near;
    float _far;

    //for orthogonal projection
    float _left, _right, _top, _bottom;

    void setOrthographic(float left, float right, float bottom, float top, float near_plane, float far_plane);
    void setPerspective(float fov, float aspect, float near_plane, float far_plane);

    void processKeyboard(Camera_Movement direction, const float dt);
    void rotate(float xoffset, float yoffset, bool constrainPitch = true);

    glm::mat4 getView();
    glm::mat4 getProjection();

private:

    void updateCameraVectors();
};