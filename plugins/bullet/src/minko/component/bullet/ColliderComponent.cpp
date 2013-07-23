/*
Copyright (c) 2013 Aerys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "ColliderComponent.hpp"

#include <minko/math/Matrix4x4.hpp>
#include <minko/scene/Node.hpp>
#include <minko/scene/NodeSet.hpp>
#include <minko/component/Transform.hpp>
#include <minko/component/bullet/Collider.hpp>
#include <minko/component/bullet/PhysicsWorld.hpp>

using namespace minko;
using namespace minko::math;
using namespace minko::scene;
using namespace minko::component;

bullet::ColliderComponent::ColliderComponent(Collider::Ptr collider):
	AbstractComponent(),
	_collider(collider),
	_physicsWorld(nullptr),
	_targetTransform(nullptr),
	_targetAddedSlot(nullptr),
	_targetRemovedSlot(nullptr),
	_addedSlot(nullptr),
	_removedSlot(nullptr),
	_colliderTrfChangedSlot(nullptr),
	_graphicsTransformChangedSlot(nullptr)
{
	if (collider == nullptr)
		throw std::invalid_argument("collider");
}

void
bullet::ColliderComponent::initialize()
{
	_targetAddedSlot	= targetAdded()->connect(std::bind(
		&bullet::ColliderComponent::targetAddedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
		));

	_targetRemovedSlot	= targetRemoved()->connect(std::bind(
		&bullet::ColliderComponent::targetRemovedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
		));

	_colliderTrfChangedSlot	= _collider->transformChanged()->connect(std::bind(
		&bullet::ColliderComponent::colliderTransformChangedHandler,
		shared_from_this(),
		std::placeholders::_1
		));

	_graphicsTransformChangedSlot	= _collider->graphicsWorldTransformChanged()->connect(std::bind(
		&bullet::ColliderComponent::graphicsWorldTransformChangedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
		));
}

void
bullet::ColliderComponent::targetAddedHandler(
	AbstractComponent::Ptr controller, 
	Node::Ptr target)
{
	if (targets().size() > 1)
		throw std::logic_error("ColliderComponent cannot have more than one target.");

	_addedSlot	= targets().front()->added()->connect(std::bind(
		&bullet::ColliderComponent::addedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
		));

	_removedSlot	= targets().front()->removed()->connect(std::bind(
		&bullet::ColliderComponent::removedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
		));

	// initialize from node if possible (mostly for adding a controller to the camera)
	initializeFromTarget(target);
}

void
bullet::ColliderComponent::targetRemovedHandler(
	AbstractComponent::Ptr controller, 
	Node::Ptr target)
{
	_addedSlot						= nullptr;
	_removedSlot					= nullptr;
	//_colliderTrfChangedSlot			= nullptr;
	//_graphicsTransformChangedSlot	= nullptr;
}

void 
bullet::ColliderComponent::addedHandler(
	Node::Ptr node, 
	Node::Ptr target, 
	Node::Ptr parent)
{
	initializeFromTarget(node);
	/*
	if (!target->hasComponent<Transform>())
		target->addComponent(Transform::create());
	
	_targetTransform	= node->component<Transform>();

	auto nodeSet	= NodeSet::create(node)
		->ancestors(true)
		->where([](Node::Ptr node)
		{
			return node->hasComponent<bullet::PhysicsWorld>();
		});
	if (nodeSet->nodes().size() != 1)
	{
		std::stringstream stream;
		stream << "ColliderComponent requires exactly one PhysicsWorld among the descendants of its target node. Found " << nodeSet->nodes().size();
		//throw std::logic_error(stream.str());
	}
	else
	{
		updateColliderWorldTransform();

		_physicsWorld	= nodeSet->nodes().front()->component<bullet::PhysicsWorld>();

		_physicsWorld->addChild(_collider);
	}
	*/
}

void
bullet::ColliderComponent::initializeFromTarget(Node::Ptr node)
{
	if (_targetTransform != nullptr && _physicsWorld != nullptr)
		return;

	if (!node->hasComponent<Transform>())
		node->addComponent(Transform::create());
	
	_targetTransform = node->component<Transform>();

	auto nodeSet = NodeSet::create(node)
		->ancestors(true)
		->where([](Node::Ptr node)
		{
			return node->hasComponent<bullet::PhysicsWorld>();
		});
	if (nodeSet->nodes().size() != 1)
	{
		std::stringstream stream;
		stream << "ColliderComponent requires exactly one PhysicsWorld among the descendants of its target node. Found " << nodeSet->nodes().size();
		//throw std::logic_error(stream.str());
	}
	else
	{
		_collider->setName(node->name());

		updateColliderWorldTransform();

		_physicsWorld	= nodeSet->nodes().front()->component<bullet::PhysicsWorld>();
		_physicsWorld->addChild(_collider);
	}
}

void 
bullet::ColliderComponent::updateColliderWorldTransform()
{
	if (_targetTransform == nullptr)
		throw std::logic_error("The Transform of the ColliderComponent's target is invalid.");

	// update the collider's world transform, and scale correction matrix
	auto graphicsTransform	= _targetTransform->modelToWorldMatrix(true);
	//_collider->setWorldTransform(graphicsTransform);

	_collider->initializePhysicsFromGraphicsWorldTransform(graphicsTransform);

	// inject the new collider's world transform into the physics world's simulation
	if (_physicsWorld)
		_physicsWorld->setWorldTransformFromCollider(_collider);
}

void
bullet::ColliderComponent::removedHandler(
	Node::Ptr node, 
	Node::Ptr target, 
	Node::Ptr parent)
{
	_physicsWorld->removeChild(_collider);
	_physicsWorld		= nullptr;
	_targetTransform	= nullptr;
}

void 
bullet::ColliderComponent::colliderTransformChangedHandler(Collider::Ptr collider)
{
	// get the world-to-parent matrix in order to update the target's Transform
	auto worldToParentMatrix	= Matrix4x4::create()
		->copyFrom(_targetTransform->modelToWorldMatrix(true))
		->invert()
		->append(_targetTransform->transform());

	auto newTransform = Matrix4x4::create()
		->copyFrom(collider->worldTransform())
		->append(worldToParentMatrix);

	_targetTransform->transform()->copyFrom(newTransform);
}

void
bullet::ColliderComponent::graphicsWorldTransformChangedHandler(Collider::Ptr collider, 
																Matrix4x4::Ptr graphicsTransform)
{
	if (_targetTransform == nullptr)
		return;

	// get the world-to-parent matrix in order to update the target's Transform
	auto worldToParentMatrix	= Matrix4x4::create()
		->copyFrom(_targetTransform->modelToWorldMatrix(true))
		->invert()
		->append(_targetTransform->transform());

	_targetTransform->transform()
		->copyFrom(graphicsTransform)
		->append(worldToParentMatrix);
}

void
bullet::ColliderComponent::prependLocalTranslation(Vector3::Ptr localTranslation)
{
	if (_physicsWorld != nullptr)
		_physicsWorld->prependLocalTranslation(_collider, localTranslation);
}

void
bullet::ColliderComponent::prependRotationY(float radians)
{
	if (_physicsWorld != nullptr)
		_physicsWorld->prependRotationY(_collider, radians);
}

void
bullet::ColliderComponent::applyRelativeImpulse(Vector3::Ptr localImpulse)
{
	if (_physicsWorld != nullptr)
		_physicsWorld->applyRelativeImpulse(_collider, localImpulse);
}

void
bullet::ColliderComponent::initializePhysicsFromGraphicsWorldTransform()
{
	if (_targetTransform == nullptr)
		return;

	_collider->initializePhysicsFromGraphicsWorldTransform(_targetTransform->modelToWorldMatrix(true));
}