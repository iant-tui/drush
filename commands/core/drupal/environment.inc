<?php
/**
 * @file
 *   Specific functions for a drupal 8+ environment.
 *   drush_include_engine() magically includes either this file
 *   or environment_X.inc depending on which version of drupal drush
 *   is called from.
 */

/**
 * Get complete information for all available modules.
 *
 * @param $include_hidden
 *   Boolean to indicate whether hidden modules should be excluded or not.
 * @return
 *   An array containing module info for all available modules.
 */
function drush_get_modules($include_hidden = TRUE) {
  $modules = system_rebuild_module_data();

  foreach ($modules as $key => $module) {
    $module->type = 'module';
    if ((!$include_hidden) && (isset($module->info['hidden']))) {
      unset($modules[$key]);
    }
    else {
      $module->schema_version = drupal_get_installed_schema_version($key);
    }
  }

  return $modules;
}

/**
 * Returns drupal required modules, including modules declared as required dynamically.
 */
function _drush_drupal_required_modules($module_info) {
  $required = drupal_required_modules();
  foreach ($module_info as $name => $module) {
    if (isset($module->info['required']) && $module->info['required']) {
      $required[] = $name;
    }
  }
  return array_unique($required);
}

/**
 * Return dependencies and its status for modules.
 *
 * @param $modules
 *   Array of module names
 * @param $module_info
 *   Drupal 'files' array for modules as returned by drush_get_modules().
 * @return
 *   Array with dependencies and status for $modules
 */
function drush_check_module_dependencies($modules, $module_info) {
  $status = array();
  foreach ($modules as $key => $module) {
    $dependencies = array_reverse($module_info[$module]->requires);
    $unmet_dependencies = array_diff(array_keys($dependencies), array_keys($module_info));
    if (!empty($unmet_dependencies)) {
      $status[$key]['error'] = array(
          'code' => 'DRUSH_PM_ENABLE_DEPENDENCY_NOT_FOUND',
          'message' => dt('Module !module cannot be enabled because it depends on the following modules which could not be found: !unmet_dependencies', array('!module' => $module, '!unmet_dependencies' => implode(',', $unmet_dependencies)))
      );
    }
    else {
      // check for version incompatibility
      foreach ($dependencies as $dependency_name => $v) {
        $current_version = $module_info[$dependency_name]->info['version'];
        $current_version = str_replace(drush_get_drupal_core_compatibility() . '-', '', $current_version);
        $incompatibility = drupal_check_incompatibility($v, $current_version);
        if (!is_null($incompatibility)) {
          $status[$key]['error'] = array(
            'code' => 'DRUSH_PM_ENABLE_DEPENDENCY_VERSION_MISMATCH',
            'message' => dt('Module !module cannot be enabled because it depends on !dependency !required_version but !current_version is available', array('!module' => $module, '!dependency' => $dependency_name, '!required_version' => $incompatibility, '!current_version' => $current_version))
          );
        }
      }
    }
    $status[$key]['unmet-dependencies'] = $unmet_dependencies;
    $status[$key]['dependencies'] = array_keys($dependencies);
  }

  return $status;
}

/**
 * Return dependents of modules.
 *
 * @param $modules
 *   Array of module names
 * @param $module_info
 *   Drupal 'files' array for modules as returned by drush_get_modules().
 * @return
 *   Array with dependents for each one of $modules
 */
function drush_module_dependents($modules, $module_info) {
  $dependents = array();
  foreach ($modules as $module) {
    $dependents = array_merge($dependents, drupal_map_assoc(array_keys($module_info[$module]->required_by)));
  }

  return array_unique($dependents);
}

/**
 * Returns a list of enabled modules.
 *
 * This is a wrapper for module_list().
 */
function drush_module_list() {
  return module_list('module_enabled');
}

/**
 * Determines which modules are implementing a hook.
 *
 * This is a wrapper for module_implements() but does not support the $sort or
 * $refresh parameters.
 */
function drush_module_implements($hook) {
  return module_implements($hook);
}

/**
 * Regenerate the stored list of hook implementations.
 *
 * This is a wrapper for module_implements('', FALSE, TRUE).
 * This is a wrapper for \Drupal::moduleHandler()->resetImplementations().
 */
function drush_module_implements_reset() {
  \Drupal::moduleHandler()->resetImplementations();
}

/**
 * Invokes a hook in a particular module.
 *
 * All arguments are passed by value. Use drupal_alter() if you need to pass
 * arguments by reference.
 *
 * This is a wrapper for module_invoke().
 */
function drush_module_invoke($module, $hook) {
  $args = func_get_args();
  // Remove $module and $hook from the arguments.
  unset($args[0], $args[1]);
  return call_user_func_array('module_invoke', $args);
}

/**
 * Invokes a hook in all enabled modules that implement it.
 *
 * All arguments are passed by value. Use drupal_alter() if you need to pass
 * arguments by reference.
 *
 * This is a wrapper for module_invoke_all().
 */
function drush_module_invoke_all($hook) {
  $args = func_get_args();
  // Remove $hook from the arguments.
  array_shift($args);
  return call_user_func_array('module_invoke_all', $hook, $args);
}

/**
 * Passes alterable variables to specific hook_TYPE_alter() implementations.
 *
 * This is a wrapper for drupal_alter().
 */
function drush_drupal_alter($type, &$data, &$context1 = NULL, &$context2 = NULL) {
  return drupal_alter($type, $data, $context1, $context2);
}

/**
 * Determines whether a given module exists.
 *
 * This is a wrapper for module_exists().
 */
function drush_module_exists($module) {
  return module_exists($module);
}

/**
 * Return a list of modules from a list of named modules.
 * Both enabled and disabled/uninstalled modules are returned.
 */
function drush_get_named_extensions_list($extensions) {
  $result = array();
  $modules = drush_get_modules();
  foreach($modules as $name => $module) {
    if (in_array($name, $extensions)) {
      $result[$name] = $module;
    }
  }
  $themes = drush_get_themes();
  foreach($themes as $name => $theme) {
    if (in_array($name, $extensions)) {
      $result[$name] = $theme;
    }
  }
  return $result;
}

/**
 * Enable a list of modules. It is assumed the list contains all the dependencies not already enabled.
 *
 * @param $modules
 *   Array of module names
 */
function drush_module_enable($modules) {
  // The list of modules already have all the dependencies, but they might not
  // be in the correct order. Still pass $enable_dependencies = TRUE so that
  // Drupal will enable the modules in the correct order.
  module_install($modules);
  // Flush all caches.
  drupal_flush_all_caches();
}

/**
 * Disable a list of modules. It is assumed the list contains all dependents not already disabled.
 *
 * @param $modules
 *   Array of module names
 */
function drush_module_disable($modules) {
  drush_set_error('DRUSH_MODULE_DISABLE', dt('Drupal 8 does not support disabling modules.'));
}

/**
 * Uninstall a list of modules.
 *
 * @param $modules
 *   Array of module names
 */
function drush_module_uninstall($modules) {
  module_uninstall($modules);
}

/**
 * Get complete information for all available themes.
 *
 * @param $include_hidden
 *   Boolean to indicate whether hidden themes should be excluded or not.
 * @return
 *   An array containing theme info for all available themes.
 */
function drush_get_themes($include_hidden = TRUE) {
  $themes = system_rebuild_theme_data();
  foreach ($themes as $key => $theme) {
    $theme->type = 'theme';
    if (!$include_hidden) {
      if (isset($theme->info['hidden'])) {
        unset($themes[$key]);
      }
    }
  }

  return $themes;
}

/**
 * Enable a list of themes.
 *
 * @param $themes
 *  Array of theme names.
 */
function drush_theme_enable($themes) {
  theme_enable($themes);
}

/**
 * Disable a list of themes.
 *
 * @param $themes
 *  Array of theme names.
 */
function drush_theme_disable($themes) {
  theme_disable($themes);
}

/**
 * Helper function to obtain the severity levels based on Drupal version.
 *
 * This is a copy of watchdog_severity_levels() without t().
 *
 * Severity levels, as defined in RFC 3164: http://www.ietf.org/rfc/rfc3164.txt.
 *
 * @return
 *   Array of watchdog severity levels.
 */
function drush_watchdog_severity_levels() {
  return array(
    WATCHDOG_EMERGENCY=> 'emergency',
    WATCHDOG_ALERT    => 'alert',
    WATCHDOG_CRITICAL => 'critical',
    WATCHDOG_ERROR    => 'error',
    WATCHDOG_WARNING  => 'warning',
    WATCHDOG_NOTICE   => 'notice',
    WATCHDOG_INFO     => 'info',
    WATCHDOG_DEBUG    => 'debug',
  );
}

/**
 * Helper function to obtain the message types based on drupal version.
 *
 * @return
 *   Array of watchdog message types.
 */
function drush_watchdog_message_types() {
  return _dblog_get_message_types();
}

/**
 * Returns a queue object.
 */
function drush_queue_get($name) {
  return \Drupal::queue($name);
}

/**
 * Returns all defined queues.
 */
function drush_queue_get_queues() {
  static $queues;
  if (!isset($queues)) {
    $queues = \Drupal::moduleHandler()->invokeAll('queue_info');
    drupal_alter('queue_info', $queues);
  }
  return $queues;
}

function _drush_theme_default() {
  return \Drupal::config('system.theme')->get('default');
}

function _drush_theme_admin() {
  $theme = \Drupal::config('system.theme')->get('admin');
  return empty($theme) ? 'seven' : $theme;
}

function _drush_file_public_path() {
  return settings()->get('file_public_path', conf_path() . '/files');
}

function _drush_file_private_path() {
  return \Drupal::config('system.file')->get('path.private');
}

