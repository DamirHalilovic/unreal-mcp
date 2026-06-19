"""
Blueprint Tools for Unreal MCP.

This module provides tools for creating and manipulating Blueprint assets in Unreal Engine.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_blueprint_tools(mcp: FastMCP):
    """Register Blueprint tools with the MCP server."""
    
    @mcp.tool()
    def create_blueprint(
        ctx: Context,
        name: str,
        parent_class: str
    ) -> Dict[str, Any]:
        """Create a new Blueprint class."""
        # Import inside function to avoid circular imports
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("create_blueprint", {
                "name": name,
                "parent_class": parent_class
            })
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Blueprint creation response: {response}")
            return response or {}
            
        except Exception as e:
            error_msg = f"Error creating blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_component_to_blueprint(
        ctx: Context,
        blueprint_name: str,
        component_type: str,
        component_name: str,
        location: List[float] = [],
        rotation: List[float] = [],
        scale: List[float] = [],
        component_properties: Dict[str, Any] = {}
    ) -> Dict[str, Any]:
        """
        Add a component to a Blueprint.
        
        Args:
            blueprint_name: Name of the target Blueprint
            component_type: Type of component to add (use component class name without U prefix)
            component_name: Name for the new component
            location: [X, Y, Z] coordinates for component's position
            rotation: [Pitch, Yaw, Roll] values for component's rotation
            scale: [X, Y, Z] values for component's scale
            component_properties: Additional properties to set on the component
        
        Returns:
            Information about the added component
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Ensure all parameters are properly formatted
            params = {
                "blueprint_name": blueprint_name,
                "component_type": component_type,
                "component_name": component_name,
                "location": location or [0.0, 0.0, 0.0],
                "rotation": rotation or [0.0, 0.0, 0.0],
                "scale": scale or [1.0, 1.0, 1.0]
            }
            
            # Add component_properties if provided
            if component_properties and len(component_properties) > 0:
                params["component_properties"] = component_properties
            
            # Validate location, rotation, and scale formats
            for param_name in ["location", "rotation", "scale"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            logger.info(f"Adding component to blueprint with params: {params}")
            response = unreal.send_command("add_component_to_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Component addition response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding component to blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_static_mesh_properties(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        static_mesh: str = "/Engine/BasicShapes/Cube.Cube"
    ) -> Dict[str, Any]:
        """
        Set static mesh properties on a StaticMeshComponent.
        
        Args:
            blueprint_name: Name of the target Blueprint
            component_name: Name of the StaticMeshComponent
            static_mesh: Path to the static mesh asset (e.g., "/Engine/BasicShapes/Cube.Cube")
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "static_mesh": static_mesh
            }
            
            logger.info(f"Setting static mesh properties with params: {params}")
            response = unreal.send_command("set_static_mesh_properties", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set static mesh properties response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting static mesh properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_component_property(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        property_name: str,
        property_value,
    ) -> Dict[str, Any]:
        """Set a property on a component in a Blueprint."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "property_name": property_name,
                "property_value": property_value
            }
            
            logger.info(f"Setting component property with params: {params}")
            response = unreal.send_command("set_component_property", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set component property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting component property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_physics_properties(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        simulate_physics: bool = True,
        gravity_enabled: bool = True,
        mass: float = 1.0,
        linear_damping: float = 0.01,
        angular_damping: float = 0.0
    ) -> Dict[str, Any]:
        """Set physics properties on a component."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "simulate_physics": simulate_physics,
                "gravity_enabled": gravity_enabled,
                "mass": float(mass),
                "linear_damping": float(linear_damping),
                "angular_damping": float(angular_damping)
            }
            
            logger.info(f"Setting physics properties with params: {params}")
            response = unreal.send_command("set_physics_properties", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set physics properties response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting physics properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def compile_blueprint(
        ctx: Context,
        blueprint_name: str
    ) -> Dict[str, Any]:
        """Compile a Blueprint."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name
            }
            
            logger.info(f"Compiling blueprint: {blueprint_name}")
            response = unreal.send_command("compile_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Compile blueprint response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error compiling blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_info(
        ctx: Context,
        blueprint_name: str
    ) -> Dict[str, Any]:
        """Introspect an existing Blueprint: components, variables, functions, and graphs.

        Args:
            blueprint_name: Bare asset name (e.g. CH_FirstPersonPlayable), a full
                object path, or a name under /Game/Blueprints/. Resolved project-wide.

        Returns name, path, parent_class, components (name/class/attached_to),
        variables (name/type), functions (name/num_nodes), and a graphs breakdown
        (event_graphs, function_graphs, macro_graphs).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_blueprint_info", {"blueprint_name": blueprint_name})

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get blueprint info response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error getting blueprint info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_graph(
        ctx: Context,
        blueprint_name: str,
        graph_name: str = ""
    ) -> Dict[str, Any]:
        """Dump one graph of a Blueprint at node + pin-connection level, for tracing logic.

        Graphs include event graphs, functions, the construction script
        (UserConstructionScript), macros, delegate signatures, and nested
        (collapsed) subgraphs.

        Args:
            blueprint_name: Bare asset name, full object path, or /Game/Blueprints/ name.
            graph_name: The graph to dump (e.g. "CanHardFlinch", "EventGraph",
                "UserConstructionScript"). Leave EMPTY to first LIST every graph
                (name + category + num_nodes) and choose one. On a miss, the error
                lists the available graphs.

        With graph_name: returns blueprint, graph, category, num_nodes, and nodes[]
        where each node has id (GUID), title, class, and pins[] (name, direction
        in/out, type, default, and links[] = {node, pin} of every connected pin).
        Prefer a specific function/macro over the full EventGraph, which can be huge.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_blueprint_graph",
                {"blueprint_name": blueprint_name, "graph_name": graph_name},
            )

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get blueprint graph response received ({len(str(response))} chars)")
            return response

        except Exception as e:
            error_msg = f"Error getting blueprint graph: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def diff_blueprint(
        ctx: Context,
        blueprint_name: str,
        base_version_path: str,
        graph_name: str = ""
    ) -> Dict[str, Any]:
        """Structured graph diff of a Blueprint against an older version, using UE's
        own graph differ (FGraphDiffControl) — the same one the visual diff tool uses.

        Output is O(changes), not O(blueprint): only actual differences are returned,
        each as a classified, pre-described entry (no re-parsing both graphs).

        Args:
            blueprint_name: The CURRENT (live) blueprint — bare name, path, or
                /Game/ name. This is the 'new' side.
            base_version_path: Absolute path to a .uasset of the OLDER revision to
                compare against (the 'base' side). Produce it with svn cat / the
                bpdiff helper, e.g. ...\\Temp\\bpdiff\\CH_FirstPersonPlayable_r214701.uasset
            graph_name: Optional — restrict the diff to a single graph.

        Returns blueprint, base, graphs_compared, num_differences, and differences[]
        where each entry has graph, category (addition/subtraction/modification/minor),
        a human display string, and the base_node/current_node GUIDs (same GUID
        vocabulary as get_blueprint_graph, so you can drill into a changed node).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "diff_blueprint",
                {"blueprint_name": blueprint_name, "base_version_path": base_version_path, "graph_name": graph_name},
            )

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Diff blueprint response received ({len(str(response))} chars)")
            return response

        except Exception as e:
            error_msg = f"Error diffing blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_asset_info(
        ctx: Context,
        asset_name: str
    ) -> Dict[str, Any]:
        """Inspect ANY asset — the tool detects the type itself, so you don't have to
        guess from the .uasset. Use this first when you don't know what an asset is.

        For a Blueprint it returns the full blueprint info (components/variables/
        functions/graphs). For anything else (data asset, curve, texture, mesh, …) it
        returns asset_type, parent_classes, and a properties[] dump (editable settings:
        name/type/value) — settings, not pixels/geometry.

        Args:
            asset_name: Bare asset name, full object path, or /Game/ path.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("get_asset_info", {"asset_name": asset_name})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            logger.info(f"Get asset info response received ({len(str(response))} chars)")
            return response
        except Exception as e:
            error_msg = f"Error getting asset info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_behavior_tree(
        ctx: Context,
        behavior_tree_name: str
    ) -> Dict[str, Any]:
        """Walk a Behavior Tree asset's node structure — the omission the other graph
        tools left. Detects composites/tasks/decorators/services and their parameters
        the same way the BT editor shows them.

        Args:
            behavior_tree_name: Bare asset name (e.g. BT_Enemy), full object path, or
                /Game/ path. Resolved project-wide.

        Returns name, path, blackboard (the linked UBlackboardData), root_decorators[],
        and root: a recursive node tree. Each node has kind (composite/task), class,
        name, description (UE's GetStaticDescription — includes the blackboard keys it
        reads/writes), services[] on composites/tasks, and children[] where each child
        carries its decorators[] (the conditions guarding that branch) and node. Child
        order is top-to-bottom execution order. Pair with get_blackboard for the keys.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_behavior_tree", {"behavior_tree_name": behavior_tree_name})

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get behavior tree response received ({len(str(response))} chars)")
            return response

        except Exception as e:
            error_msg = f"Error getting behavior tree: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blackboard(
        ctx: Context,
        blackboard_name: str
    ) -> Dict[str, Any]:
        """Dump a Blackboard (UBlackboardData) asset's keys — the data a Behavior Tree
        reads and writes.

        Args:
            blackboard_name: Bare asset name (e.g. BB_Enemy), full object path, or
                /Game/ path. Resolved project-wide.

        Returns name, path, parent (the blackboard it inherits from, if any), num_keys,
        and keys[]: each has name, type (Object/Bool/Vector/Enum/…), description,
        instance_synced (synchronized across instances), and inherited (true for keys
        from a parent blackboard, with from = the parent's name). Parent keys are listed
        first so the list reads root-down.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_blackboard", {"blackboard_name": blackboard_name})

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get blackboard response received ({len(str(response))} chars)")
            return response

        except Exception as e:
            error_msg = f"Error getting blackboard: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def diff_asset(
        ctx: Context,
        base_version_path: str,
        asset_name: str = "",
        current_version_path: str = "",
        graph_name: str = ""
    ) -> Dict[str, Any]:
        """Structured diff of ANY asset against an older version — the tool detects the
        type itself and routes: Blueprints get a graph diff (FGraphDiffControl) plus a
        property/CDO diff; actors get actor-level + per-component diffs (transform/scale/
        mesh/materials and components added/removed); data assets/curves/textures/meshes
        get a property ('settings') diff via CompareUnrelatedObjects. Not a visual diff.

        Use this instead of diff_blueprint when you don't know the asset's type — a
        .uasset is opaque until loaded.

        Specify the CURRENT side ONE of two ways:
          - asset_name: a live, content-browser asset (blueprint, texture, data asset).
          - current_version_path: a .uasset FILE. REQUIRED for World Partition external
            actors and maps, which aren't resolvable as live assets — pass the
            working-copy file (e.g. Project/Content/__ExternalActors__/.../<GUID>.uasset).

        Args:
            base_version_path: Absolute path to a .uasset of the OLDER revision
                (make it with `svn cat -r REV <url> > file` or bpdiff.ps1).
            asset_name: Current live asset (bare name / path). Omit for external actors.
            current_version_path: Current .uasset file (use for external actors/maps).
            graph_name: Optional — restrict the (blueprint) graph diff to one graph.

        Returns asset, asset_type, base, property_differences[] (property, change, and
        base_value/current_value); for blueprints also graph_differences[]; for actors
        also component_differences[] (component, property, change, base_value/
        current_value — incl. RelativeLocation/Rotation/Scale3D — or component_added/
        component_removed).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "diff_asset",
                {"asset_name": asset_name, "current_version_path": current_version_path,
                 "base_version_path": base_version_path, "graph_name": graph_name},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            logger.info(f"Diff asset response received ({len(str(response))} chars)")
            return response
        except Exception as e:
            error_msg = f"Error diffing asset: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_blueprint_property(
        ctx: Context,
        blueprint_name: str,
        property_name: str,
        property_value
    ) -> Dict[str, Any]:
        """
        Set a property on a Blueprint class default object.
        
        Args:
            blueprint_name: Name of the target Blueprint
            property_name: Name of the property to set
            property_value: Value to set the property to
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "property_name": property_name,
                "property_value": property_value
            }
            
            logger.info(f"Setting blueprint property with params: {params}")
            response = unreal.send_command("set_blueprint_property", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set blueprint property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting blueprint property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # @mcp.tool() commented out, just use set_component_property instead
    def set_pawn_properties(
        ctx: Context,
        blueprint_name: str,
        auto_possess_player: str = "",
        use_controller_rotation_yaw: bool = None,
        use_controller_rotation_pitch: bool = None,
        use_controller_rotation_roll: bool = None,
        can_be_damaged: bool = None
    ) -> Dict[str, Any]:
        """
        Set common Pawn properties on a Blueprint.
        This is a utility function that sets multiple pawn-related properties at once.
        
        Args:
            blueprint_name: Name of the target Blueprint (must be a Pawn or Character)
            auto_possess_player: Auto possess player setting (None, "Disabled", "Player0", "Player1", etc.)
            use_controller_rotation_yaw: Whether the pawn should use the controller's yaw rotation
            use_controller_rotation_pitch: Whether the pawn should use the controller's pitch rotation
            use_controller_rotation_roll: Whether the pawn should use the controller's roll rotation
            can_be_damaged: Whether the pawn can be damaged
            
        Returns:
            Response indicating success or failure with detailed results for each property
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Define the properties to set
            properties = {}
            if auto_possess_player and auto_possess_player != "":
                properties["auto_possess_player"] = auto_possess_player
            
            # Only include boolean properties if they were explicitly set
            if use_controller_rotation_yaw is not None:
                properties["bUseControllerRotationYaw"] = use_controller_rotation_yaw
            if use_controller_rotation_pitch is not None:
                properties["bUseControllerRotationPitch"] = use_controller_rotation_pitch
            if use_controller_rotation_roll is not None:
                properties["bUseControllerRotationRoll"] = use_controller_rotation_roll
            if can_be_damaged is not None:
                properties["bCanBeDamaged"] = can_be_damaged
                
            if not properties:
                logger.warning("No properties specified to set")
                return {"success": True, "message": "No properties specified to set", "results": {}}
            
            # Set each property using the generic set_blueprint_property function
            results = {}
            overall_success = True
            
            for prop_name, prop_value in properties.items():
                params = {
                    "blueprint_name": blueprint_name,
                    "property_name": prop_name,
                    "property_value": prop_value
                }
                
                logger.info(f"Setting pawn property {prop_name} to {prop_value}")
                response = unreal.send_command("set_blueprint_property", params)
                
                if not response:
                    logger.error(f"No response from Unreal Engine for property {prop_name}")
                    results[prop_name] = {"success": False, "message": "No response from Unreal Engine"}
                    overall_success = False
                    continue
                
                results[prop_name] = response
                if not response.get("success", False):
                    overall_success = False
            
            return {
                "success": overall_success,
                "message": "Pawn properties set" if overall_success else "Some pawn properties failed to set",
                "results": results
            }
            
        except Exception as e:
            error_msg = f"Error setting pawn properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    logger.info("Blueprint tools registered successfully") 