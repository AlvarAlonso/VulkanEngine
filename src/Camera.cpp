#include "Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera() : _position(glm::vec3(0, 20, 20)), _direction(glm::vec3(0, 0, -1)), _speed(SPEED), _sensitivity(SENSITIVITY)
{
    _up_vec = glm::vec3(0, 1, 0);
    _yaw = YAW;
    _pitch = PITCH;
    glm::lookAt(_position, _position + _direction, glm::vec3(0, 1, 0));
    setPerspective(60.0f, 1920.0f / 1080.0f, 0.1f, 512.0f);
    updateCameraVectors();
}

void Camera::processKeyboard(Camera_Movement direction, const float dt)
{
    float movementSpeed = _speed * dt;
    if (direction == FORWARD)
        _position += _direction * movementSpeed;
    if (direction == BACKWARD)
        _position -= _direction * movementSpeed;
    if (direction == RIGHT)
        _position += _right_vec * movementSpeed;
    if (direction == LEFT)
        _position -= _right_vec * movementSpeed;
    if (direction == UP)
        _position += _up_vec * movementSpeed;
    if (direction == DOWN)
        _position -= _up_vec * movementSpeed;
}

void Camera::rotate(float xoffset, float yoffset, bool constrainPitch)
{
    xoffset *= _sensitivity;
    yoffset *= _sensitivity;

    _yaw += xoffset;
    _pitch -= yoffset;

    if (constrainPitch) {
        if (_pitch > 89.0f)
            _pitch = 89.0f;
        if (_pitch < -89.0f)
            _pitch = -89.9f;
    }

    updateCameraVectors();
}

glm::mat4 Camera::getView()
{
    return glm::lookAt(_position, _position + _direction, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::getProjection()
{
    if(_type == PERSPECTIVE)
    {
        glm::mat4x4&& projection = glm::perspective(glm::radians(_fov), _aspect, _near, _far);
        projection[1][1] *= -1;

        return projection;
    }
    else
    {
        glm::mat4x4 M = glm::mat4x4(0);
        
        M[0][0] = 2.0f / (_right - _left);
        M[3][0] = -(_right + _left) / (_right - _left);
        M[1][1] = 2.0f / (_top - _bottom);
        M[3][1] = -(_top + _bottom) / (_top - _bottom);
        M[2][2] = -2.0f / (_far - _near);
        M[3][2] = -(_far + _near) / (_far - _near);
        M[3][3] = 1.0f;
        //M[1][1] *= -1;

        //return glm::transpose(M);
        //return M;
        //return glm::ortho(_left, _right, _bottom, _top, -2000.0f, 100.0f);
        return glm::ortho(-100.0f, 100.0f, -100.0f / 1.88f, 100.0f / 1.88f, -2000.0f, 100.0f);
        //return glm::ortho(_left, _right, _bottom, _top);
    }
}

void Camera::updateCameraVectors()
{
    glm::vec3 front;
    front.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    front.y = sin(glm::radians(_pitch));
    front.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));

    _direction = glm::normalize(front);
    _right_vec = glm::normalize(glm::cross(_direction, glm::vec3(0, 1, 0)));
    _up_vec = glm::normalize(glm::cross(_right_vec, _direction));
}

void Camera::setOrthographic(float left, float right, float bottom, float top, float near_plane, float far_plane)
{
    _type = ORTHOGRAPHIC;

    this->_left = left;
    this->_right = right;
    this->_bottom = bottom;
    this->_top = top;
    this->_near = near_plane;
    this->_far = far_plane;
}

void Camera::setPerspective(float fov, float aspect, float near_plane, float far_plane)
{
    _type = PERSPECTIVE;

    this->_fov = fov;
    this->_aspect = aspect;
    this->_near = near_plane;
    this->_far = far_plane;
}